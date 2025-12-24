// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <vector>

using namespace archive_r;

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <directory_path>" << std::endl;
    return 1;
  }

  std::string dir_path = argv[1];
  const std::filesystem::path base_path(dir_path);

  try {
    Traverser traverser({ make_single_path(dir_path) });

    struct ObservedEntry {
      std::string relative_path;
      size_t depth;
      bool is_file;
      uint64_t size;
    };

    std::vector<ObservedEntry> observed;
    size_t max_depth = 0;

    const auto compute_relative = [&](Entry &entry, const std::string &fallback) {
      std::filesystem::path reconstructed;
      const auto &hierarchy = entry.path_hierarchy();
      for (const auto &component : hierarchy) {
        const std::string component_str = path_entry_display(component);
        if (component_str.empty()) {
          continue;
        }
        reconstructed /= component_str;
      }

      if (!hierarchy.empty()) {
        try {
          const auto relative_fs = reconstructed.lexically_relative(base_path);
          if (!relative_fs.empty() && relative_fs != std::filesystem::path(".")) {
            return relative_fs.generic_string();
          }
        } catch (const std::exception &) {
          // Fallback logic below handles unexpected path states.
        }
        if (reconstructed == base_path) {
          return std::string();
        }
      }

      if (fallback == dir_path) {
        return std::string();
      }

      const std::string prefix = dir_path + "/";
      if (fallback.rfind(prefix, 0) == 0) {
        return fallback.substr(prefix.size());
      }
      return fallback;
    };

    for (Entry &entry : traverser) {
      max_depth = std::max(max_depth, entry.depth());
      const std::string full_path = entry.path();
      observed.push_back({
          compute_relative(entry, full_path),
          entry.depth(),
          entry.is_file(),
          entry.size(),
      });
    }

    const std::string dirname = std::filesystem::path(dir_path).filename().string();
    struct ExpectedEntry {
      std::string relative_path;
      size_t depth;
      bool is_file;
      uint64_t size;
    };

    const std::unordered_map<std::string, std::pair<size_t, std::vector<ExpectedEntry>>> expectations = {
      {
          "directory_test",
          {
              1,
              {
                  { "", 0, false, 0 },
                  { "archive1.tar.gz", 0, true, 146 },
                  { "archive1.tar.gz/dir_file_a.txt", 1, true, 22 },
                  { "archive1.tar.gz/dir_file_b.txt", 1, true, 22 },
                  { "archive2.tar.gz", 0, true, 145 },
                  { "archive2.tar.gz/dir_file_c.txt", 1, true, 22 },
                  { "archive2.tar.gz/dir_file_d.txt", 1, true, 22 },
                  { "subdir", 0, false, 0 },
                  { "subdir/nested_archive.tar.gz", 0, true, 125 },
                  { "subdir/nested_archive.tar.gz/nested_file.txt", 1, true, 25 },
              },
          },
      },
    };

    auto it = expectations.find(dirname);
    if (it != expectations.end()) {
      const auto &expected_pair = it->second;
      const size_t expected_max_depth = expected_pair.first;
      const auto &expected_entries = expected_pair.second;
      if (observed.size() != expected_entries.size()) {
        std::cerr << "Entry count mismatch for " << dirname << ": expected " << expected_entries.size() << ", got " << observed.size() << std::endl;
        return 1;
      }
      if (max_depth != expected_max_depth) {
        std::cerr << "Max depth mismatch for " << dirname << ": expected " << expected_max_depth << ", got " << max_depth << std::endl;
        return 1;
      }

      std::unordered_map<std::string, ExpectedEntry> expected_by_path;
      for (const auto &expected : expected_entries) {
        expected_by_path.emplace(expected.relative_path, expected);
      }

      for (size_t i = 0; i < observed.size(); ++i) {
        const auto &actual = observed[i];
        auto expected_it = expected_by_path.find(actual.relative_path);
        if (expected_it == expected_by_path.end()) {
          std::cerr << "Unexpected entry path: " << actual.relative_path << std::endl;
          return 1;
        }

        const auto &expected = expected_it->second;
        if (actual.depth != expected.depth) {
          std::cerr << "Depth mismatch for " << actual.relative_path << ": expected " << expected.depth << ", got " << actual.depth << std::endl;
          return 1;
        }
        if (actual.is_file != expected.is_file) {
          std::cerr << "Type mismatch for " << actual.relative_path << std::endl;
          return 1;
        }
        if (actual.size != expected.size) {
          std::cerr << "Size mismatch for " << actual.relative_path << ": expected " << expected.size << ", got " << actual.size << std::endl;
          return 1;
        }

        expected_by_path.erase(expected_it);
      }

      if (!expected_by_path.empty()) {
        std::cerr << "Missing expected entries. Example: " << expected_by_path.begin()->first << std::endl;
        return 1;
      }
    } else {
      if (observed.empty()) {
        std::cerr << "Directory navigation produced no entries for " << dir_path << std::endl;
        return 1;
      }
    }

    std::cout << "Directory navigation succeeded (entries=" << observed.size() << ", max_depth=" << max_depth << ")" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
