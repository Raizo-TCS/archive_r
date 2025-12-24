// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"
#include <filesystem>
#include <iostream>
#include <locale.h>
#include <unordered_map>

using namespace archive_r;

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");

  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <archive_path>" << std::endl;
    return 1;
  }

  std::string archive_path = argv[1];

  try {
    Traverser traverser({ make_single_path(archive_path) });
    size_t count = 0;
    size_t empty_name_count = 0;

    // Use iterator API
    for (Entry &entry : traverser) {
      count++;
      const auto &hierarchy = entry.path_hierarchy();
      bool has_empty_name = hierarchy.empty();
      if (!has_empty_name) {
        const std::string tail_display = path_entry_display(hierarchy.back());
        if (tail_display.empty()) {
          has_empty_name = true;
        }
      }
      if (has_empty_name) {
        empty_name_count++;
        std::cerr << "Empty name at entry " << count << std::endl;
      }
    }

    const std::string filename = std::filesystem::path(archive_path).filename().string();
    struct Expectation {
      size_t entries;
      size_t empty_names;
    };

    const std::unordered_map<std::string, Expectation> expectations = {
      { "deeply_nested.tar.gz", { 11, 0 } },
      { "nested_with_multi_volume.tar.gz", { 9, 0 } },
      { "deeply_nested_multi_volume.tar.gz", { 12, 0 } },
      { "multi_volume_test.tar.gz", { 4, 0 } },
    };

    auto it = expectations.find(filename);
    if (it != expectations.end()) {
      const Expectation &expected = it->second;
      if (count != expected.entries) {
        std::cerr << "Entry count mismatch for " << filename << ": expected " << expected.entries << ", got " << count << std::endl;
        return 1;
      }
      if (empty_name_count != expected.empty_names) {
        std::cerr << "Empty name count mismatch for " << filename << ": expected " << expected.empty_names << ", got " << empty_name_count << std::endl;
        return 1;
      }
    } else {
      if (count == 0) {
        std::cerr << "Traversal produced no entries for " << archive_path << std::endl;
        return 1;
      }
      if (empty_name_count != 0) {
        std::cerr << "Traversal found " << empty_name_count << " entries without names for " << archive_path << std::endl;
        return 1;
      }
    }

    std::cout << "Entry counting succeeded (entries=" << count << ", empty_names=" << empty_name_count << ")" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
