// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

/**
 * @file test_stress_multipart_source.cc
 * @brief Validation test for stress_test_ultimate.tar.gz split into multi-volume files
 *
 * Test Data Files:
 * - Input: test_data/stress_test_ultimate.tar.gz.part001 (10KB)
 *          test_data/stress_test_ultimate.tar.gz.part002 (10KB)
 *          test_data/stress_test_ultimate.tar.gz.part003 (10KB)
 *          test_data/stress_test_ultimate.tar.gz.part004 (2.3KB)
 * - Total size: 32.3KB (same as single file: stress_test_ultimate.tar.gz 33KB)
 * - Created by: split -b 10k stress_test_ultimate.tar.gz (Oct 23, 2025)
 *
 * This test verifies that multi-volume source files produce exactly the same results
 * as the single-file archive (stress_test_ultimate.tar.gz).
 *
 * Expected Results (identical to single-file version):
 * - Total entries: 365 (includes all .partXXX files as entries)
 * - Maximum depth: 12
 * - All critical paths present
 * - All multi-volume archives inside properly expanded
 * - Depth distribution matches
 *
 * Key Validation:
 * This test ensures that ArchiveStackOrchestrator properly handles multi-volume source files
 * at the root level, producing identical traversal results to a single archive file.
 */

#include "../src/archive_stack_orchestrator.h"
#include "archive_r/path_hierarchy_utils.h"
#include "multi_volume_utils.h"
#include <algorithm>
#include <archive_entry.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace archive_r;
namespace fs = std::filesystem;

namespace {

PathHierarchy collapse_sources(const std::vector<PathHierarchy> &sources) {
  if (sources.empty()) {
    return {};
  }

  PathHierarchy collapsed = collapse_multi_volume_sources(sources);
  if (!collapsed.empty()) {
    return collapsed;
  }

  return sources.front();
}

} // namespace

// Expected structure characteristics
// Note: These values match the single-file archive (stress_test_ultimate.tar.gz)
// The total entry count is 365 because:
// - Multi-volume .partXXX files are counted as individual entries
// - Each .partXXX file appears in the traversal before descending into it
// - Example: ultimate_multi_1.part001, part002, part003, part004 are 4 entries
//            plus the content inside .part001 (ultimate_multi_1.txt) is another entry
// - Single-file version has the same count when traversed the same way
const int EXPECTED_TOTAL_ENTRIES = 365; // Total entries including .partXXX files
const int EXPECTED_MAX_DEPTH = 12;      // Maximum nesting depth

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

  // Deep directory paths with multi-volume archives
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz/"
  "deep_dirs.tar.gz/very/long/directory/path/structure/that/goes/deep/into/filesystem/"
  "deep_multi.part001",
  "level1_final.tar.gz/level2_multi_set.tar.gz/level3_alternating.tar.gz/level4_deep_paths.tar.gz/"
  "deep_dirs.tar.gz/very/long/directory/path/structure/that/goes/deep/into/filesystem/"
  "deep_multi.part002",

  // Another deep path with multi-volume archives
  "level1_final.tar.gz/level2_multi_set.tar.gz/deep_path_2.tar.gz/another/deep/path/to/test/"
  "traversal/capabilities/path_multi.part001",
  "level1_final.tar.gz/level2_multi_set.tar.gz/deep_path_2.tar.gz/another/deep/path/to/test/"
  "traversal/capabilities/path_multi.part002",
};

// Multi-volume groups that must be properly detected and combined
const std::vector<std::string> MULTI_VOLUME_BASES = {
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

// Expected depth distribution (includes multi-volume .partXXX files)
const std::map<int, int> EXPECTED_DEPTH_DISTRIBUTION = {
  { 1, 21 },  // Depth 1: 21 entries
  { 2, 18 },  // Depth 2: 18 entries
  { 3, 20 },  // Depth 3: 20 entries
  { 4, 24 },  // Depth 4: 24 entries after directory flattening
  { 5, 8 },   // Depth 5: 8 entries
  { 6, 21 },  // Depth 6: 21 entries (includes extra multi-volume files)
  { 7, 21 },  // Depth 7: 21 entries (includes multi-volume files)
  { 8, 126 }, // Depth 8: 126 entries (includes many multi-volume files)
  { 9, 43 },  // Depth 9: 43 entries (includes multi-volume files)
  { 10, 39 }, // Depth 10: 39 entries (includes multi-volume files)
  { 11, 17 }, // Depth 11: 17 entries (includes multi-volume files)
  { 12, 7 },  // Depth 12: 7 entries (deepest files)
};

struct EntryInfo {
  PathHierarchy hierarchy;
  std::string path;
  uint64_t size = 0;
  mode_t filetype = 0;
  size_t depth = 0;
};

static std::string hierarchy_to_path(const PathHierarchy &hierarchy) {
  if (hierarchy.empty()) {
    return "";
  }

  std::ostringstream oss;
  for (size_t i = 0; i < hierarchy.size(); ++i) {
    if (i > 0) {
      oss << '/';
    }
    const PathEntry &component = hierarchy[i];
    if (component.is_single()) {
      const std::string &value = component.single_value();
      std::filesystem::path component_path(value);
      if (i == 0 && component_path.has_filename() && component_path.is_absolute()) {
        oss << component_path.filename().string();
      } else {
        oss << value;
      }
    } else {
      oss << path_entry_display(component);
    }
  }
  return oss.str();
}

class ValidationCollector {
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

  bool on_entry(const EntryInfo &entry) {
    total_entries++;
    int depth = static_cast<int>(entry.depth);

    if (depth > max_depth) {
      max_depth = depth;
    }

    depth_distribution[depth]++;

    std::string path = entry.path;
    all_paths.push_back(path);

    // Check if this is a critical path
    for (auto &pair : found_critical_paths) {
      if (path.find(pair.first) != std::string::npos) {
        pair.second = true;
      }
    }

    return true; // Continue traversal
  }
};

class OrchestratorTraverser {
public:
  OrchestratorTraverser(std::shared_ptr<ArchiveStackOrchestrator> orchestrator, std::string root_name) {
    if (!orchestrator) {
      throw std::invalid_argument("orchestrator cannot be null");
    }
    ArchiveLevel level;
    level.orchestrator = std::move(orchestrator);
    level.name = std::move(root_name);
    _archive_stack.push_back(std::move(level));
    advance_to_first();
  }

  bool at_end() const { return _at_end; }

  const EntryInfo &current() const {
    if (!_has_entry) {
      throw std::logic_error("No current entry available");
    }
    return _current_entry;
  }

  void advance() {
    if (_at_end) {
      return;
    }
    advance_archive_mode();
  }

private:
  struct ArchiveLevel {
    std::shared_ptr<ArchiveStackOrchestrator> orchestrator;
    std::string name;
  };

  void advance_to_first() {
    if (!_archive_stack.empty() && _archive_stack.back().orchestrator->advance(false)) {
      create_entry_from_archive();
    } else {
      _at_end = true;
      _has_entry = false;
    }
  }

  void advance_archive_mode() {
    if (_archive_stack.empty()) {
      _at_end = true;
      _has_entry = false;
      return;
    }

    auto orchestrator = _archive_stack.back().orchestrator;
    if (_has_entry && orchestrator && _current_entry.filetype == AE_IFREG) {
      std::string entry_name = orchestrator->current_entryname();
      auto *head = orchestrator->active_head();
      if (head && head->descend()) {

        ArchiveLevel level;
        level.orchestrator = orchestrator;
        level.name = entry_name;
        _archive_stack.push_back(std::move(level));

        if (_archive_stack.back().orchestrator->advance(false)) {
          create_entry_from_archive();
          return;
        }
        pop_archive_level();
      }
    }

    if (!_archive_stack.empty() && _archive_stack.back().orchestrator->advance(false)) {
      create_entry_from_archive();
      return;
    }

    while (true) {
      if (try_process_multi_volume_groups()) {
        return;
      }

      if (_archive_stack.empty()) {
        break;
      }

      pop_archive_level();

      if (_archive_stack.empty()) {
        break;
      }

      if (_archive_stack.back().orchestrator->advance(false)) {
        create_entry_from_archive();
        return;
      }
    }

    _at_end = true;
    _has_entry = false;
  }

  bool try_process_multi_volume_groups() {
    if (_archive_stack.empty()) {
      return false;
    }

    auto orchestrator = _archive_stack.back().orchestrator;
    if (!orchestrator) {
      return false;
    }

    size_t depth = orchestrator->depth();
    PathHierarchy parent_hierarchy;
    if (auto *head = orchestrator->active_head()) {
      PathHierarchy current_path = head->current_entry_hierarchy();
      if (current_path.size() >= depth) {
        parent_hierarchy.assign(current_path.begin(), current_path.begin() + static_cast<PathHierarchy::difference_type>(depth));
      } else {
        parent_hierarchy = current_path;
      }
    }

    auto &groups = orchestrator->_multi_volume_groups[parent_hierarchy];

    for (auto it = groups.begin(); it != groups.end();) {
      auto group = it->second;
      it = groups.erase(it);

      auto *head = orchestrator->active_head();
      if (!head) {
        continue;
      }

      try {
        const PathHierarchy collapsed = collapse_sources(group.parts);
        if (collapsed.empty()) {
          continue;
        }
        head->push_archive_from_sources(collapsed);
      } catch (const std::exception &) {
        continue;
      }

      ArchiveLevel level;
      level.orchestrator = orchestrator;
      level.name = group.base_name;
      _archive_stack.push_back(std::move(level));

      if (_archive_stack.back().orchestrator->advance(false)) {
        create_entry_from_archive();
        return true;
      }

      pop_archive_level();
    }

    if (groups.empty()) {
      orchestrator->_multi_volume_groups.erase(parent_hierarchy);
    }

    return false;
  }

  void create_entry_from_archive() {
    if (_archive_stack.empty()) {
      _at_end = true;
      _has_entry = false;
      return;
    }

    auto orchestrator = _archive_stack.back().orchestrator;
    auto *archive = orchestrator ? orchestrator->current_archive() : nullptr;
    if (!archive) {
      _at_end = true;
      _has_entry = false;
      return;
    }

    PathHierarchy hierarchy = orchestrator->current_entry_hierarchy();
    if (hierarchy.empty()) {
      for (const auto &level : _archive_stack) {
        hierarchy.emplace_back(PathEntry::single(level.name));
      }
      hierarchy.emplace_back(PathEntry::single(orchestrator->current_entryname()));
    }

    _current_entry.hierarchy = hierarchy;
    _current_entry.path = hierarchy_to_path(hierarchy);
    _current_entry.size = archive->current_entry_size();
    _current_entry.filetype = archive->current_entry_filetype();
    _current_entry.depth = hierarchy.empty() ? 0 : hierarchy.size() - 1;
    _has_entry = true;
    _at_end = false;
  }

  void pop_archive_level() {
    if (_archive_stack.empty()) {
      return;
    }

    auto orchestrator = _archive_stack.back().orchestrator;
    if (orchestrator) {
      PathHierarchy parent_hierarchy;
      if (auto *head = orchestrator->active_head()) {
        parent_hierarchy = head->current_entry_hierarchy();
        if (!parent_hierarchy.empty()) {
          parent_hierarchy.pop_back();
        }
      }
      orchestrator->_multi_volume_groups.erase(parent_hierarchy);
    }

    if (_archive_stack.size() > 1 && orchestrator) {
      if (auto *head = orchestrator->active_head()) {
        head->ascend();
      }
    }

    _archive_stack.pop_back();
  }

  std::vector<ArchiveLevel> _archive_stack;
  bool _at_end = false;
  bool _has_entry = false;
  EntryInfo _current_entry;
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

bool validate_multi_volume_expansion(const ValidationCollector &collector) {
  std::cout << "\n=== Multi-Volume Expansion Validation ===" << std::endl;

  int validated_multi_volume_sets = 0;

  for (const auto &base : MULTI_VOLUME_BASES) {
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
      validated_multi_volume_sets++;
    } else if (part001_found) {
      std::cerr << "✗ " << base << " - found but not expanded" << std::endl;
    } else {
      std::cerr << "✗ " << base << " - not found" << std::endl;
    }
  }

  std::cout << "\nValidated multi-volume sets: " << validated_multi_volume_sets << "/" << MULTI_VOLUME_BASES.size() << std::endl;

  if (validated_multi_volume_sets != static_cast<int>(MULTI_VOLUME_BASES.size())) {
    std::cerr << "✗ FAILED: Some multi-volume sets were not properly expanded!" << std::endl;
    return false;
  }

  std::cout << "✓ PASSED: All multi-volume sets expanded correctly" << std::endl;
  return true;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <path_to_stress_test_ultimate.tar.gz.part001>" << std::endl;
    std::cerr << "\nExample:" << std::endl;
    std::cerr << "  " << argv[0] << " test_data/stress_test_ultimate.tar.gz.part001" << std::endl;
    std::cerr << "\nNote: This test requires all 4 part files to be present:" << std::endl;
    std::cerr << "  - stress_test_ultimate.tar.gz.part001 (10KB)" << std::endl;
    std::cerr << "  - stress_test_ultimate.tar.gz.part002 (10KB)" << std::endl;
    std::cerr << "  - stress_test_ultimate.tar.gz.part003 (10KB)" << std::endl;
    std::cerr << "  - stress_test_ultimate.tar.gz.part004 (2.3KB)" << std::endl;
    std::cerr << "  Total: 32.3KB (MD5: 63d836fbbce1d1e8bfc955ebe65ba9f7)" << std::endl;
    return 1;
  }

  std::string part001_path = argv[1];

  // Collect all multi-volume files using multi_volume_utils pattern matching
  // Expected: 4 files (part001-004) totaling 32.3KB
  // - stress_test_ultimate.tar.gz.part001 (10KB)
  // - stress_test_ultimate.tar.gz.part002 (10KB)
  // - stress_test_ultimate.tar.gz.part003 (10KB)
  // - stress_test_ultimate.tar.gz.part004 (2.3KB)
  std::vector<std::string> part_files;
  try {
    part_files = collect_multi_volume_files(part001_path);
  } catch (const std::exception &e) {
    std::cerr << "✗ ERROR: Failed to collect part files: " << e.what() << std::endl;
    return 1;
  }

  if (part_files.empty()) {
    std::cerr << "✗ ERROR: No part files found for: " << part001_path << std::endl;
    return 1;
  }

  std::cout << "============================================================" << std::endl;
  std::cout << "  Stress Test Multi-Volume Source Validation" << std::endl;
  std::cout << "============================================================" << std::endl;
  std::cout << "Multi-volume archive parts found: " << part_files.size() << std::endl;
  for (size_t i = 0; i < part_files.size(); i++) {
    std::cout << "  Part " << (i + 1) << ": " << part_files[i] << std::endl;
  }

  // Extract base name for multi-volume archive
  std::string base_name = extract_multi_volume_base_name(fs::path(part001_path).filename().string());

  std::cout << "Base name: " << base_name << std::endl;

  // Phase 1: Create ArchiveStackOrchestrator for multi-volume source
  std::cout << "\n=== Phase 1: Opening multi-volume archive ===" << std::endl;

  // Convert file paths to PathHierarchy (single-element hierarchies)
  std::vector<PathHierarchy> part_hierarchies;
  for (const auto &part_file : part_files) {
    part_hierarchies.push_back(make_single_path(part_file));
  }

  std::shared_ptr<ArchiveStackOrchestrator> orchestrator;
  try {
    orchestrator = std::make_shared<ArchiveStackOrchestrator>(part_hierarchies, base_name);
    std::cout << "✓ Successfully created ArchiveStackOrchestrator for multi-volume archive" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "✗ FATAL ERROR creating orchestrator: " << e.what() << std::endl;
    return 1;
  }

  // Phase 2: Traverse and collect entries
  std::cout << "\n=== Phase 2: Traversing archive ===" << std::endl;
  ValidationCollector collector;
  int entry_count = 0;
  try {
    OrchestratorTraverser traverser(orchestrator, base_name);
    while (!traverser.at_end()) {
      const EntryInfo &entry = traverser.current();
      entry_count++;
      std::cout << "Entry #" << entry_count << ": " << entry.path << std::endl;
      std::cout << "  Depth: " << entry.depth << std::endl;
      std::cout << "  Size: " << entry.size << " bytes" << std::endl;
      std::cout << "  Type: " << (entry.filetype == AE_IFDIR ? "directory" : "file") << std::endl;

      if (!collector.on_entry(entry)) {
        break;
      }

      traverser.advance();
    }
  } catch (const std::exception &e) {
    std::cerr << "✗ FATAL ERROR during traversal: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Traversal complete. Collected " << collector.total_entries << " entries." << std::endl;

  // Phase 3: Validate structure
  std::cout << "\n=== Phase 3: Validating structure ===" << std::endl;

  bool all_passed = true;

  all_passed &= validate_entries(collector);
  all_passed &= validate_depth(collector);
  all_passed &= validate_depth_distribution(collector);
  all_passed &= validate_critical_paths(collector);
  all_passed &= validate_multi_volume_expansion(collector);

  // Final summary
  std::cout << "\n============================================================" << std::endl;
  if (all_passed) {
    std::cout << "✓ ALL VALIDATIONS PASSED" << std::endl;
    std::cout << "  Multi-volume source produces identical results to single file!" << std::endl;
    std::cout << "============================================================" << std::endl;
    return 0;
  } else {
    std::cerr << "✗ SOME VALIDATIONS FAILED" << std::endl;
    std::cerr << "============================================================" << std::endl;
    return 1;
  }
}
