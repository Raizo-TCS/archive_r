// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <locale.h>
#include <string>
#include <unordered_map>
#include <vector>

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
    struct ObservedEntry {
      std::string relative_path;
      size_t depth;
      bool is_directory;
      uint64_t size;
    };

    std::vector<ObservedEntry> observed;
    const std::string archive_filename = std::filesystem::path(archive_path).filename().string();
    const auto make_relative = [&](const std::string &full_path) {
      if (full_path == archive_path) {
        return archive_filename;
      }
      const std::string prefix = archive_path + "/";
      if (full_path.rfind(prefix, 0) == 0) {
        return full_path.substr(prefix.size());
      }
      return full_path;
    };

    for (Entry &entry : traverser) {
      const std::string full_path = hierarchy_display(entry.path_hierarchy());
      observed.push_back({
          make_relative(full_path),
          entry.depth(),
          entry.is_directory(),
          entry.size(),
      });
    }

    const std::string filename = std::filesystem::path(archive_path).filename().string();
    struct ExpectedEntry {
      std::string relative_path;
      size_t depth;
      bool is_directory;
      uint64_t size;
    };

    const std::unordered_map<std::string, std::vector<ExpectedEntry>> expectations = {
      {
          "deeply_nested.tar.gz",
          {
              { "level1.tar.gz", 1, false, 571 },
              { "level1.tar.gz/level2.tar.gz", 2, false, 402 },
              { "level1.tar.gz/level2.tar.gz/level3.tar.gz", 3, false, 121 },
              { "level1.tar.gz/level2.tar.gz/level3.tar.gz/deep.txt", 4, false, 13 },
              { "level1.tar.gz/level2.tar.gz/level3.tar.gz/deep.txt/deep", 5, false, 0 },
              { "level1.tar.gz/level2.tar.gz/archive.tar.gz.part001", 3, false, 100 },
              { "level1.tar.gz/level2.tar.gz/archive.tar.gz.part002", 3, false, 13 },
              { "level1.tar.gz/level2.tar.gz/archive.tar.gz.part003", 3, false, 0 },
              { "level1.tar.gz/root.txt", 2, false, 10 },
              { "root.txt", 1, false, 10 },
          },
      },
    };

    auto it = expectations.find(filename);
    if (it != expectations.end()) {
      std::vector<ExpectedEntry> expected_entries;
      std::error_code size_ec;
      const auto archive_size = std::filesystem::file_size(archive_path, size_ec);
      if (!size_ec) {
        expected_entries.push_back({ filename, 0, false, static_cast<uint64_t>(archive_size) });
      } else {
        expected_entries.push_back({ filename, 0, false, 0 });
      }
      expected_entries.insert(expected_entries.end(), it->second.begin(), it->second.end());
      if (observed.size() != expected_entries.size()) {
        std::cerr << "Iterator entry count mismatch for " << filename << ": expected " << expected_entries.size() << ", got " << observed.size() << std::endl;
        return 1;
      }

      for (size_t i = 0; i < expected_entries.size(); ++i) {
        const auto &expected = expected_entries[i];
        const auto &actual = observed[i];
        if (actual.relative_path != expected.relative_path) {
          std::cerr << "Entry " << i + 1 << " path mismatch: expected " << expected.relative_path << ", got " << actual.relative_path << std::endl;
          return 1;
        }
        if (actual.depth != expected.depth) {
          std::cerr << "Entry " << i + 1 << " depth mismatch for " << actual.relative_path << ": expected " << expected.depth << ", got " << actual.depth << std::endl;
          return 1;
        }
        if (actual.is_directory != expected.is_directory) {
          std::cerr << "Entry " << i + 1 << " type mismatch for " << actual.relative_path << std::endl;
          return 1;
        }
        if (actual.size != expected.size) {
          std::cerr << "Entry " << i + 1 << " size mismatch for " << actual.relative_path << ": expected " << expected.size << ", got " << actual.size << std::endl;
          return 1;
        }
      }
    } else {
      if (observed.empty()) {
        std::cerr << "Iterator produced no entries for " << archive_path << std::endl;
        return 1;
      }
    }

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
