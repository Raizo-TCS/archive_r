// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

/**
 * @file test_stress_ultimate_verification.cc
 * @brief Comprehensive verification test for stress_test_ultimate.tar.gz
 *
 * This test verifies:
 * 1. Total entry count (expected: 349 entries)
 * 2. Maximum depth reached (expected: depth 12)
 * 3. Depth distribution
 * 4. Presence of all expected archives at each level
 * 5. Multipart archive completeness (all parts present)
 * 6. Deep directory path traversal
 * 7. Level 10 deepest files accessibility
 * 8. All 10 levels properly nested
 */

#include "archive_r/traverser.h"
#include "archive_r/path_hierarchy_utils.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace archive_r;

// Test result tracking
struct TestResult {
  std::string test_name;
  bool passed;
  std::string message;
};

std::vector<TestResult> test_results;

void log_test(const std::string &name, bool passed, const std::string &message = "") {
  test_results.push_back({ name, passed, message });
  if (passed) {
    std::cout << "  ✓ " << name << std::endl;
  } else {
    std::cout << "  ✗ " << name;
    if (!message.empty()) {
      std::cout << ": " << message;
    }
    std::cout << std::endl;
  }
}

void print_summary() {
  std::cout << "\n========================================" << std::endl;
  std::cout << "  TEST SUMMARY" << std::endl;
  std::cout << "========================================" << std::endl;

  int passed = 0;
  int failed = 0;

  for (const auto &result : test_results) {
    if (result.passed) {
      passed++;
    } else {
      failed++;
    }
  }

  std::cout << "Total:  " << test_results.size() << std::endl;
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  std::cout << std::endl;

  if (failed > 0) {
    std::cout << "Failed tests:" << std::endl;
    for (const auto &result : test_results) {
      if (!result.passed) {
        std::cout << "  - " << result.test_name;
        if (!result.message.empty()) {
          std::cout << ": " << result.message;
        }
        std::cout << std::endl;
      }
    }
  }
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <stress_test_ultimate.tar.gz>" << std::endl;
    return 1;
  }

  std::string archive_path = argv[1];

  std::cout << "=== Stress Test Ultimate Verification ===" << std::endl;
  std::cout << "Archive: " << archive_path << std::endl;
  std::cout << std::endl;

  // Data collection
  int total_entries = 0;
  int max_depth = 0;
  std::map<int, int> depth_distribution;
  std::set<std::string> all_paths;
  std::map<std::string, std::vector<std::string>> multipart_groups;

  // Expected files tracking
  std::set<std::string> found_archives;
  std::set<std::string> found_multiparts;

  // Traverse and collect data
  std::cout << "Phase 1: Traversing archive and collecting data..." << std::endl;

  try {
    Traverser traverser({ make_single_path(archive_path) });

    for (Entry &entry : traverser) {
      std::string path = entry.path();
      int depth = entry.depth();

      total_entries++;
      all_paths.insert(path);
      depth_distribution[depth]++;

      if (depth > max_depth) {
        max_depth = depth;
      }

      // Track archives
      if (path.find(".tar.gz") != std::string::npos) {
        const auto &hier = entry.path_hierarchy();
        if (!hier.empty()) {
          const std::string filename = path_entry_display(hier.back());
          found_archives.insert(filename);
        }
      }

      // Track multipart files
      if (path.find(".part") != std::string::npos) {
        const auto &hier = entry.path_hierarchy();
        if (!hier.empty()) {
          std::string filename = path_entry_display(hier.back());

          // Extract just the filename (last component after last /)
          size_t slash_pos = filename.rfind('/');
          if (slash_pos != std::string::npos) {
            filename = filename.substr(slash_pos + 1);
          }

          // Extract base name (before .partXXX)
          size_t part_pos = filename.find(".part");
          if (part_pos != std::string::npos) {
            std::string base_filename = filename.substr(0, part_pos);
            found_multiparts.insert(base_filename);

            // Track full path for group counting
            std::string base_path = path.substr(0, path.find(".part"));
            multipart_groups[base_path].push_back(path);
          }
        }
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Error during traversal: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "  Collected " << total_entries << " entries" << std::endl;
  std::cout << "  Max depth: " << max_depth << std::endl;
  std::cout << std::endl;

  // ========================================
  // Test 1: Total Entry Count
  // ========================================
  std::cout << "Test 1: Total Entry Count" << std::endl;
  const int EXPECTED_ENTRIES = 349; // Includes root archive entry plus all multipart entries
  log_test("Total entries", total_entries == EXPECTED_ENTRIES, "Expected " + std::to_string(EXPECTED_ENTRIES) + ", got " + std::to_string(total_entries));
  std::cout << std::endl;

  // ========================================
  // Test 2: Maximum Depth
  // ========================================
  std::cout << "Test 2: Maximum Depth" << std::endl;
  const int EXPECTED_MAX_DEPTH = 12; // Updated: level10_archive files are now correctly detected at depth 12
  log_test("Maximum depth", max_depth == EXPECTED_MAX_DEPTH, "Expected depth " + std::to_string(EXPECTED_MAX_DEPTH) + ", got " + std::to_string(max_depth));
  std::cout << std::endl;

  // ========================================
  // Test 3: Depth Distribution
  // ========================================
  std::cout << "Test 3: Depth Distribution" << std::endl;
  std::cout << "  Depth distribution:" << std::endl;
  for (const auto &[depth, count] : depth_distribution) {
    std::cout << "    Depth " << depth << ": " << count << " entries" << std::endl;
  }

  // Verify all depths from 1 to 11 exist
  bool all_depths_present = true;
  for (int d = 1; d <= EXPECTED_MAX_DEPTH; d++) {
    if (depth_distribution.find(d) == depth_distribution.end() || depth_distribution[d] == 0) {
      all_depths_present = false;
      break;
    }
  }
  log_test("All depths 1-12 present", all_depths_present);
  std::cout << std::endl;

  // ========================================
  // Test 4: Level 10 Files (Deepest)
  // ========================================
  std::cout << "Test 4: Level 10 Files Accessibility" << std::endl;

  std::vector<std::string> level10_files = { "deep_file_001.txt", "deep_file_002.txt", "deep_file_003.txt" };

  for (const auto &filename : level10_files) {
    bool found = false;
    for (const auto &path : all_paths) {
      if (path.find("level10_archive.tar.gz/" + filename) != std::string::npos) {
        found = true;
        break;
      }
    }
    log_test("Level 10 file: " + filename, found);
  }
  std::cout << std::endl;

  // ========================================
  // Test 5: All Level Archives Present
  // ========================================
  std::cout << "Test 5: All Level Archives Present" << std::endl;

  std::vector<std::string> required_archives = { "level1_final.tar.gz",      "level2_multi_set.tar.gz",     "level3_alternating.tar.gz", "level4_deep_paths.tar.gz", "level5_multi_nest.tar.gz",
                                                 "level6_tiny_parts.tar.gz", "level7_many_archives.tar.gz", "level8_big_parts.tar.gz",   "level9_container.tar.gz",  "level10_archive.tar.gz" };

  for (const auto &archive : required_archives) {
    bool found = (found_archives.find(archive) != found_archives.end());
    log_test("Archive: " + archive, found);
  }
  std::cout << std::endl;

  // ========================================
  // Test 6: Extra Archives (8 sets)
  // ========================================
  std::cout << "Test 6: Extra Archives (8 sets)" << std::endl;

  for (int i = 1; i <= 8; i++) {
    std::string archive = "extra_archive_" + std::to_string(i) + ".tar.gz";
    bool found = (found_archives.find(archive) != found_archives.end());
    log_test("Extra archive " + std::to_string(i), found);
  }
  std::cout << std::endl;

  // ========================================
  // Test 7: Level 7 Archives (12 sets)
  // ========================================
  std::cout << "Test 7: Level 7 Archives (12 archives)" << std::endl;

  for (int i = 1; i <= 12; i++) {
    std::string archive = "archive_";
    if (i < 10)
      archive += "0";
    archive += std::to_string(i) + ".tar.gz";
    bool found = (found_archives.find(archive) != found_archives.end());
    log_test("Level 7 archive " + std::to_string(i), found);
  }
  std::cout << std::endl;

  // ========================================
  // Test 8: Multipart Archive Completeness
  // ========================================
  std::cout << "Test 8: Multipart Archive Completeness" << std::endl;

  // Expected multipart groups: base_name -> expected part count
  // For multiparts that appear in multiple locations, we verify each group individually
  std::map<std::string, int> expected_group_counts;

  // Build expected counts from actual groups
  for (const auto &[group_base, parts] : multipart_groups) {
    // Extract base name from full path
    size_t last_slash = group_base.rfind('/');
    std::string base_name = (last_slash != std::string::npos) ? group_base.substr(last_slash + 1) : group_base;

    // Store the full group path with expected count
    expected_group_counts[group_base] = parts.size();
  }

  // Define expected multipart base names and their part counts
  std::map<std::string, int> expected_multiparts
      = { { "ultimate_multi_1", 4 }, { "ultimate_multi_2", 4 }, { "ultimate_multi_3", 4 }, { "root_multipart", 4 }, { "multi_set_1", 3 },         { "multi_set_2", 3 },
          { "multi_set_3", 3 },      { "multi_set_4", 3 },      { "multi_set_5", 3 },      { "path_multi", 2 },     { "alternating_multi_1", 3 }, { "alternating_multi_2", 3 },
          { "deep_multi", 2 },       { "nested_multi", 3 },     { "extra_multi", 2 },      { "tiny_parts", 8 }, // May appear in multiple locations
          { "large_multi", 4 },      { "multi_small", 5 } };

  // Verify each expected multipart base name exists
  for (const auto &[base_name, expected_parts] : expected_multiparts) {
    bool found_base = (found_multiparts.find(base_name) != found_multiparts.end());

    if (!found_base) {
      log_test("Multipart: " + base_name, false, "Not found");
      continue;
    }

    // Find all groups matching this base name (exact match on the group's base name)
    std::vector<std::pair<std::string, int>> matching_groups;
    for (const auto &[group_base, parts] : multipart_groups) {
      // Extract the actual base name from the group path
      size_t last_slash = group_base.rfind('/');
      std::string group_name = (last_slash != std::string::npos) ? group_base.substr(last_slash + 1) : group_base;

      // Exact match on base name
      if (group_name == base_name) {
        matching_groups.push_back({ group_base, parts.size() });
      }
    }

    // Verify each group has the expected part count
    bool all_correct = true;
    for (const auto &[group_path, actual_parts] : matching_groups) {
      if (actual_parts != expected_parts) {
        all_correct = false;
        // Extract readable path for error message
        size_t pos = group_path.find(base_name);
        std::string context = (pos != std::string::npos && pos > 50) ? "..." + group_path.substr(pos - 20) : group_path;
        log_test("Multipart: " + base_name + " at " + context + " (" + std::to_string(expected_parts) + " parts)", false,
                 "Expected " + std::to_string(expected_parts) + " parts, got " + std::to_string(actual_parts));
      }
    }

    if (all_correct && !matching_groups.empty()) {
      std::string label = "Multipart: " + base_name + " (" + std::to_string(expected_parts) + " parts)";
      if (matching_groups.size() > 1) {
        label += " [" + std::to_string(matching_groups.size()) + " groups]";
      }
      log_test(label, true, "");
    }
  }
  std::cout << std::endl;

  // ========================================
  // Test 9: Deep Directory Paths
  // ========================================
  std::cout << "Test 9: Deep Directory Path Traversal" << std::endl;

  // Check for deep directory structure in level 4
  std::vector<std::string> deep_path_markers = { "very/long/directory/path/structure/that/goes/deep/into/filesystem", "another/deep/path/to/test/traversal/capabilities" };

  for (const auto &marker : deep_path_markers) {
    bool found = false;
    for (const auto &path : all_paths) {
      if (path.find(marker) != std::string::npos) {
        found = true;
        break;
      }
    }
    log_test("Deep path: " + marker, found);
  }

  // Check for files in deep paths
  std::vector<std::string> deep_path_files = { "deeply_nested_file.txt", "deep_nested.tar.gz", "deep_multi.part001", "deep_multi.part002", "path_archive.tar.gz",
                                               "path_multi.part001",     "path_multi.part002", "path_regular.txt",   "path_content.txt" };

  for (const auto &file : deep_path_files) {
    bool found = false;
    for (const auto &path : all_paths) {
      if (path.find(file) != std::string::npos) {
        found = true;
        break;
      }
    }
    log_test("Deep path file: " + file, found);
  }
  std::cout << std::endl;

  // ========================================
  // Test 10: Regular Archives in Alternating Pattern
  // ========================================
  std::cout << "Test 10: Alternating Pattern Archives" << std::endl;

  std::vector<std::string> alternating_archives = { "regular_archive_1.tar.gz", "regular_archive_2.tar.gz" };

  for (const auto &archive : alternating_archives) {
    bool found = (found_archives.find(archive) != found_archives.end());
    log_test("Alternating archive: " + archive, found);
  }
  std::cout << std::endl;

  // ========================================
  // Test 11: Root Level Archives
  // ========================================
  std::cout << "Test 11: Root Level Archives" << std::endl;

  std::vector<std::string> root_archives = { "root_archive_1.tar.gz", "root_archive_2.tar.gz" };

  for (const auto &archive : root_archives) {
    bool found = (found_archives.find(archive) != found_archives.end());
    log_test("Root archive: " + archive, found);
  }
  std::cout << std::endl;

  // ========================================
  // Test 12: Specific Deep Nested Chain
  // ========================================
  std::cout << "Test 12: Complete Nesting Chain Verification" << std::endl;

  // Verify the complete chain from ultimate to level 10
  std::string expected_chain = "stress_test_ultimate.tar.gz/"
                               "level1_final.tar.gz/"
                               "level2_multi_set.tar.gz/"
                               "level3_alternating.tar.gz/"
                               "level4_deep_paths.tar.gz/"
                               "level5_multi_nest.tar.gz/"
                               "level6_tiny_parts.tar.gz/"
                               "level7_many_archives.tar.gz/"
                               "level8_big_parts.tar.gz/"
                               "level9_container.tar.gz/"
                               "level10_archive.tar.gz";

  bool chain_found = false;
  for (const auto &path : all_paths) {
    if (path.find(expected_chain) != std::string::npos) {
      chain_found = true;
      break;
    }
  }
  log_test("Complete 10-level nesting chain", chain_found);
  std::cout << std::endl;

  // ========================================
  // Test 13: Multipart Content Accessibility
  // ========================================
  std::cout << "Test 13: Multipart Content Accessibility" << std::endl;

  // Verify that content inside multipart archives is accessible
  std::vector<std::string> multipart_content = {
    "large_content_1.txt",        // inside large_multi
    "l9_multipart_content_1.txt", // inside multi_small
    "small_part_content.txt",     // inside tiny_parts
    "l5_data_1.txt",              // inside nested_multi
    "l5_extra.txt",               // inside extra_multi
    "deep_content_1.txt",         // inside deep_multi
    "path_content.txt",           // inside path_multi
    "root_multi_content.txt",     // inside root_multipart
    "ultimate_multi_1.txt",       // inside ultimate_multi_1
    "ultimate_multi_2.txt",       // inside ultimate_multi_2
    "ultimate_multi_3.txt"        // inside ultimate_multi_3
  };

  for (const auto &content : multipart_content) {
    bool found = false;
    for (const auto &path : all_paths) {
      if (path.find(content) != std::string::npos) {
        found = true;
        break;
      }
    }
    log_test("Multipart content: " + content, found);
  }
  std::cout << std::endl;

  // ========================================
  // Test 14: Archive Content Files
  // ========================================
  std::cout << "Test 14: Archive Content Files" << std::endl;

  std::vector<std::string> archive_content = {
    "extra_file_1.txt", "extra_file_8.txt",
    "file_01_a.txt",   // level 7
    "file_12_a.txt",   // level 7
    "l9_file_a.txt",   // level 9
    "regular_1.txt",   // level 3
    "regular_2.txt",   // level 3
    "root_file_1.txt", // level 1
    "root_file_2.txt"  // level 1
  };

  for (const auto &file : archive_content) {
    bool found = false;
    for (const auto &path : all_paths) {
      if (path.find(file) != std::string::npos) {
        found = true;
        break;
      }
    }
    log_test("Archive content: " + file, found);
  }
  std::cout << std::endl;

  // Print summary
  print_summary();

  // Return exit code
  int failed_count = 0;
  for (const auto &result : test_results) {
    if (!result.passed) {
      failed_count++;
    }
  }

  if (failed_count == 0) {
    std::cout << "✓ All verification tests passed!" << std::endl;
    return 0;
  } else {
    std::cout << "✗ " << failed_count << " verification test(s) failed" << std::endl;
    return 1;
  }
}
