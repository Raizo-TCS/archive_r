// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

/**
 * @file test_stress_ultimate_validation.cc
 * @brief Comprehensive validation test for stress_test_ultimate.tar.gz
 *
 * This test verifies that stress_test_ultimate.tar.gz:
 * 1. Has the expected number of total entries (348)
 * 2. Reaches the expected maximum depth (12)
 * 3. Contains specific critical files at various depths
 * 4. Properly expands multipart archives
 * 5. Traverses deep directory paths with archives/multipart
 */

#include "archive_r/entry.h"
#include "archive_r/traverser.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace archive_r;

// Expected structure characteristics
const int EXPECTED_TOTAL_ENTRIES = 348;
const int EXPECTED_MAX_DEPTH = 12;

// Critical paths that must exist (samples from each level)
const std::vector<std::string> CRITICAL_PATHS = {
  // Level 1: Top level
  "extra_archive_1.tar.gz",
  "level1_final.tar.gz",
  "ultimate_multi_1.part001",
  "ultimate_multi_1.part002",

  // Level 2: Inside level1_final
  "level1_final.tar.gz/level2_multi_set.tar.gz",
  "level1_final.tar.gz/root_multipart.part001",
  "level1_final.tar.gz/root_archive_1.tar.gz",

  // Level 3: Inside level2_multi_set
  "level1_final.tar.gz/level2_multi_set.tar.gz/multi_set_1.part001",
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz",

  // Level 4: Deep paths
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz",

  // Level 5: level5_multi_nest
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz/"
  "level5_multi_nest.tar.gz",

  // Level 6: level6_tiny_parts
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz/"
  "level5_multi_nest.tar.gz/level6_tiny_parts.tar.gz",

  // Level 7: level7_many_archives
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz/"
  "level5_multi_nest.tar.gz/level6_tiny_parts.tar.gz/level7_many_archives.tar.gz",

  // Level 8: level8_big_parts
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz/"
  "level5_multi_nest.tar.gz/level6_tiny_parts.tar.gz/level7_many_archives.tar.gz/"
  "level8_big_parts.tar.gz",

  // Level 9: level9_container
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz/"
  "level5_multi_nest.tar.gz/level6_tiny_parts.tar.gz/level7_many_archives.tar.gz/"
  "level8_big_parts.tar.gz/level9_container.tar.gz",

  // Level 10: level10_archive (deepest archive)
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz/"
  "level5_multi_nest.tar.gz/level6_tiny_parts.tar.gz/level7_many_archives.tar.gz/"
  "level8_big_parts.tar.gz/level9_container.tar.gz/level10_archive.tar.gz",

  // Level 11: Deepest file (inside level10_archive)
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz/"
  "level5_multi_nest.tar.gz/level6_tiny_parts.tar.gz/level7_many_archives.tar.gz/"
  "level8_big_parts.tar.gz/level9_container.tar.gz/level10_archive.tar.gz/deep_file_001.txt",

  // Deep directory paths with archives
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz/"
  "deep_dirs.tar.gz/very/long/directory/path/structure/that/goes/deep/into/filesystem/"
  "deep_nested.tar.gz",

  // Deep directory paths with multipart
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz/"
  "deep_dirs.tar.gz/very/long/directory/path/structure/that/goes/deep/into/filesystem/"
  "deep_multi.part001",
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz/"
  "deep_dirs.tar.gz/very/long/directory/path/structure/that/goes/deep/into/filesystem/"
  "deep_multi.part002",

  // Another deep path with multipart
  "level1_final.tar.gz/level2_multi_set.tar.gz/deep_path_2.tar.gz/another/deep/path/to/test/"
  "traversal/capabilities/path_multi.part001",
  "level1_final.tar.gz/level2_multi_set.tar.gz/deep_path_2.tar.gz/another/deep/path/to/test/"
  "traversal/capabilities/path_multi.part002",
};

// Multipart groups that must be properly detected and combined
const std::vector<std::string> MULTIPART_BASES = {
  "ultimate_multi_1",
  "ultimate_multi_2",
  "ultimate_multi_3",
  "level1_final.tar.gz/root_multipart",
  "level1_final.tar.gz/level2_multi_set.tar.gz/multi_set_1",
  "level1_final.tar.gz/level2_multi_set.tar.gz/multi_set_2",
  "level1_final.tar.gz/level2_multi_set.tar.gz/multi_set_3",
  "level1_final.tar.gz/level2_multi_set.tar.gz/multi_set_4",
  "level1_final.tar.gz/level2_multi_set.tar.gz/multi_set_5",
};

// Expected depth distribution
const std::map<int, int> EXPECTED_DEPTH_DISTRIBUTION = {
  { 1, 21 },  // Depth 1: 21 entries
  { 2, 18 },  // Depth 2: 18 entries
  { 3, 20 },  // Depth 3: 20 entries
  { 4, 17 },  // Depth 4: 17 entries after directory flattening
  { 5, 8 },   // Depth 5: 8 entries
  { 6, 11 },  // Depth 6: 11 entries (multi-volume spans included)
  { 7, 21 },  // Depth 7: 21 entries
  { 8, 126 }, // Depth 8: 126 entries (many small archives at level 7)
  { 9, 43 },  // Depth 9: 43 entries
  { 10, 39 }, // Depth 10: 39 entries
  { 11, 17 }, // Depth 11: 17 entries
  { 12, 7 },  // Depth 12: 7 entries (deepest files)
};

class ValidationCollector : public ICallback {
public:
  int total_entries = 0;
  int max_depth = 0;
  std::map<int, int> depth_distribution;
  std::vector<std::string> all_paths;
  std::map<std::string, bool> found_critical_paths;

  ValidationCollector() {
    for (const auto &path : CRITICAL_PATHS) {
      found_critical_paths[path] = false;
    }
  }

  bool on_entry(const Entry &entry) override {
    total_entries++;
    int depth = entry.get_depth();

    if (depth > max_depth) {
      max_depth = depth;
    }

    depth_distribution[depth]++;

    std::string path = entry.get_path();
    all_paths.push_back(path);

    // Check if this is a critical path
    for (auto &pair : found_critical_paths) {
      if (path.find(pair.first) != std::string::npos) {
        pair.second = true;
      }
    }

    return true; // Continue traversal
  }

  void on_error(const std::string &path, const std::string &error) override { std::cerr << "ERROR at " << path << ": " << error << std::endl; }
};

bool validate_entries(const ValidationCollector &collector) {
  std::cout << "\n=== Entry Count Validation ===" << std::endl;
  std::cout << "Expected entries: " << EXPECTED_TOTAL_ENTRIES << std::endl;
  std::cout << "Actual entries:   " << collector.total_entries << std::endl;

  if (collector.total_entries != EXPECTED_TOTAL_ENTRIES) {
    std::cerr << "✗ FAILED: Entry count mismatch!" << std::endl;
    return false;
  }
  std::cout << "✓ PASSED: Entry count matches" << std::endl;
  return true;
}

bool validate_depth(const ValidationCollector &collector) {
  std::cout << "\n=== Depth Validation ===" << std::endl;
  std::cout << "Expected max depth: " << EXPECTED_MAX_DEPTH << std::endl;
  std::cout << "Actual max depth:   " << collector.max_depth << std::endl;

  if (collector.max_depth != EXPECTED_MAX_DEPTH) {
    std::cerr << "✗ FAILED: Max depth mismatch!" << std::endl;
    return false;
  }
  std::cout << "✓ PASSED: Max depth matches" << std::endl;
  return true;
}

bool validate_depth_distribution(const ValidationCollector &collector) {
  std::cout << "\n=== Depth Distribution Validation ===" << std::endl;

  bool all_match = true;
  for (const auto &expected : EXPECTED_DEPTH_DISTRIBUTION) {
    int depth = expected.first;
    int expected_count = expected.second;
    int actual_count = 0;

    auto it = collector.depth_distribution.find(depth);
    if (it != collector.depth_distribution.end()) {
      actual_count = it->second;
    }

    std::cout << "Depth " << depth << ": expected=" << expected_count << ", actual=" << actual_count;

    if (actual_count != expected_count) {
      std::cout << " ✗ MISMATCH" << std::endl;
      all_match = false;
    } else {
      std::cout << " ✓" << std::endl;
    }
  }

  if (!all_match) {
    std::cerr << "✗ FAILED: Depth distribution mismatch!" << std::endl;
    return false;
  }

  std::cout << "✓ PASSED: Depth distribution matches" << std::endl;
  return true;
}

bool validate_critical_paths(const ValidationCollector &collector) {
  std::cout << "\n=== Critical Paths Validation ===" << std::endl;

  int found_count = 0;
  int missing_count = 0;

  for (const auto &pair : collector.found_critical_paths) {
    if (pair.second) {
      found_count++;
    } else {
      std::cerr << "✗ MISSING: " << pair.first << std::endl;
      missing_count++;
    }
  }

  std::cout << "Found:   " << found_count << "/" << CRITICAL_PATHS.size() << std::endl;
  std::cout << "Missing: " << missing_count << std::endl;

  if (missing_count > 0) {
    std::cerr << "✗ FAILED: Some critical paths are missing!" << std::endl;
    return false;
  }

  std::cout << "✓ PASSED: All critical paths found" << std::endl;
  return true;
}

bool validate_multipart_expansion(const ValidationCollector &collector) {
  std::cout << "\n=== Multipart Expansion Validation ===" << std::endl;

  int validated_multiparts = 0;

  for (const auto &base : MULTIPART_BASES) {
    // Check if .part001 exists and has been expanded
    bool part001_found = false;
    bool content_inside_part001_found = false;

    for (const auto &path : collector.all_paths) {
      if (path.find(base + ".part001") != std::string::npos) {
        part001_found = true;

        // Check if there's content inside (indicates expansion)
        if (path.find(base + ".part001/") != std::string::npos) {
          content_inside_part001_found = true;
          break;
        }
      }
    }

    if (part001_found && content_inside_part001_found) {
      std::cout << "✓ " << base << " - expanded properly" << std::endl;
      validated_multiparts++;
    } else if (part001_found) {
      std::cerr << "✗ " << base << " - found but not expanded" << std::endl;
    } else {
      std::cerr << "✗ " << base << " - not found" << std::endl;
    }
  }

  std::cout << "\nValidated multiparts: " << validated_multiparts << "/" << MULTIPART_BASES.size() << std::endl;

  if (validated_multiparts != static_cast<int>(MULTIPART_BASES.size())) {
    std::cerr << "✗ FAILED: Some multiparts were not properly expanded!" << std::endl;
    return false;
  }

  std::cout << "✓ PASSED: All multiparts expanded correctly" << std::endl;
  return true;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <path_to_stress_test_ultimate.tar.gz>" << std::endl;
    return 1;
  }

  std::string archive_path = argv[1];

  std::cout << "============================================================" << std::endl;
  std::cout << "  Stress Test Ultimate Validation" << std::endl;
  std::cout << "============================================================" << std::endl;
  std::cout << "Archive: " << archive_path << std::endl;

  // Phase 1: Collect all entries
  std::cout << "\n=== Phase 1: Traversing archive ===" << std::endl;

  Traverser traverser;
  ValidationCollector collector;

  try {
    traverser.traverse(archive_path, &collector);
  } catch (const std::exception &e) {
    std::cerr << "✗ FATAL ERROR during traversal: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Traversal complete. Collected " << collector.total_entries << " entries." << std::endl;

  // Phase 2: Validate structure
  std::cout << "\n=== Phase 2: Validating structure ===" << std::endl;

  bool all_passed = true;

  all_passed &= validate_entries(collector);
  all_passed &= validate_depth(collector);
  all_passed &= validate_depth_distribution(collector);
  all_passed &= validate_critical_paths(collector);
  all_passed &= validate_multipart_expansion(collector);

  // Final summary
  std::cout << "\n============================================================" << std::endl;
  if (all_passed) {
    std::cout << "✓ ALL VALIDATIONS PASSED" << std::endl;
    std::cout << "============================================================" << std::endl;
    return 0;
  } else {
    std::cerr << "✗ SOME VALIDATIONS FAILED" << std::endl;
    std::cerr << "============================================================" << std::endl;
    return 1;
  }
}
