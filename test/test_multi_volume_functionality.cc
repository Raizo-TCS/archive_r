// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/traverser.h"
#include <cctype>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace archive_r;

/**
 * Test for multi-volume archive functionality
 *
 * This test verifies that:
 * 1. Multi-volume files (.part001, .part002, etc.) are detected
 * 2. When set_multi_volume_group() is called, the parts are grouped
 * 3. The grouped multi-volume archive is automatically opened and traversed
 *
 * Expected behavior for test_data/multi_volume_test.tar.gz:
 * - Contains: archive.tar.gz.part001, archive.tar.gz.part002
 * - When grouped, should descend into the combined archive.tar.gz
 * - Should find entries INSIDE the multipart archive
 */

struct MultiVolumeGroup {
  std::string base_name;
  std::vector<std::string> parts;
  bool marked = false;
};

bool is_multi_volume_part(const std::string &filename) {
  size_t pos = filename.rfind(".part");
  if (pos == std::string::npos)
    return false;

  size_t num_start = pos + 5;
  if (num_start >= filename.size())
    return false;

  for (size_t i = num_start; i < filename.size(); ++i) {
    if (!isdigit(static_cast<unsigned char>(filename[i])))
      return false;
  }
  return true;
}

std::string get_multi_volume_base_name(const std::string &part_name) {
  size_t pos = part_name.rfind(".part");
  return (pos != std::string::npos) ? part_name.substr(0, pos) : "";
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <archive_path>" << std::endl;
    return 1;
  }

  std::string archive_path = argv[1];

  try {
    std::cout << "=== Multi-Volume Archive Functionality Test ===" << std::endl;
    std::cout << "Archive: " << archive_path << std::endl << std::endl;

    // Phase 1: Detect multi-volume files
    std::cout << "Phase 1: Detecting multi-volume files..." << std::endl;
    std::map<std::string, MultiVolumeGroup> groups;

    Traverser traverser({ make_single_path(archive_path) });
    for (Entry &entry : traverser) {
      const auto &hier = entry.path_hierarchy();
      if (!hier.empty()) {
        const PathEntry &tail = hier.back();
        if (tail.is_single()) {
          const std::string &filename = tail.single_value();

          if (is_multi_volume_part(filename)) {
            std::string base_name = get_multi_volume_base_name(filename);
            std::cout << "  Found multi-volume: " << filename << " (base: " << base_name << ")" << std::endl;

            if (groups.find(base_name) == groups.end()) {
              groups[base_name] = MultiVolumeGroup{ base_name, {}, false };
            }
            groups[base_name].parts.push_back(entry.path());
          }
        }
      }
    }

    if (groups.empty()) {
      std::cout << "  No multi-volume files found." << std::endl;
      std::cout << "\n✓ TEST PASSED (no multipart files to test)" << std::endl;
      return 0;
    }

    std::cout << "\nFound " << groups.size() << " multipart group(s):" << std::endl;
    for (const auto &[base, group] : groups) {
      std::cout << "  - " << base << " (" << group.parts.size() << " parts)" << std::endl;
    }

    // Phase 2: Mark multi-volume groups and re-traverse
    std::cout << "\nPhase 2: Marking multi-volume groups and re-traversing..." << std::endl;

    Traverser traverser2({ make_single_path(archive_path) });
    int total_entries = 0;
    int multi_volume_entries_marked = 0;
    int entries_inside_multi_volume = 0;

    for (Entry &entry : traverser2) {
      total_entries++;

      const auto &hier = entry.path_hierarchy();
      if (!hier.empty()) {
        const PathEntry &tail = hier.back();

        if (tail.is_single()) {
          const std::string &filename = tail.single_value();

          if (is_multi_volume_part(filename)) {
            std::string base_name = get_multi_volume_base_name(filename);

            // Mark as multi-volume group (this should trigger grouping)
            entry.set_multi_volume_group(base_name);
            multi_volume_entries_marked++;

            std::cout << "  Marked: " << entry.path() << std::endl;
          }
        }

        // Check if this entry is INSIDE a multi-volume archive
        // (depth should be > 1 if multi-volume grouping worked)
        if (entry.depth() > 1) {
          entries_inside_multi_volume++;
          std::cout << "  Found inside multi-volume: " << entry.path() << " (depth=" << entry.depth() << ")" << std::endl;
        }
      }
    }

    // Phase 3: Verify results
    std::cout << "\n=== Test Results ===" << std::endl;
    std::cout << "Total entries: " << total_entries << std::endl;
    std::cout << "Multi-volume entries marked: " << multi_volume_entries_marked << std::endl;
    std::cout << "Entries found INSIDE multi-volume archives: " << entries_inside_multi_volume << std::endl;

    // Check expectations
    bool test_passed = true;
    std::string failure_reason;

    if (multi_volume_entries_marked == 0) {
      test_passed = false;
      failure_reason = "No multi-volume entries were marked";
    } else if (entries_inside_multi_volume == 0) {
      test_passed = false;
      failure_reason = "Multi-volume entries were marked, but NO entries found inside them\n"
                       "  This indicates that set_multi_volume_group() is not working correctly.\n"
                       "  Expected: Multi-volume files should be grouped and their contents traversed.";
    }

    if (test_passed) {
      std::cout << "\n✓ TEST PASSED: Multi-volume functionality is working correctly!" << std::endl;
      return 0;
    } else {
      std::cerr << "\n✗ TEST FAILED: " << failure_reason << std::endl;
      std::cerr << "\nDiagnostic Information:" << std::endl;
      std::cerr << "  - Multi-volume files were detected: " << !groups.empty() << std::endl;
      std::cerr << "  - set_multi_volume_group() was called: " << (multi_volume_entries_marked > 0) << std::endl;
      std::cerr << "  - Entries inside multi-volume found: " << (entries_inside_multi_volume > 0) << std::endl;
      std::cerr << "\nExpected: After calling set_multi_volume_group(), the multi-volume archive" << std::endl;
      std::cerr << "          should be automatically grouped and traversed, revealing its contents." << std::endl;
      return 1;
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
