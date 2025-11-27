// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/path_hierarchy.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"
#include <filesystem>
#include <iostream>
#include <unordered_map>
#include <vector>

using namespace archive_r;

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <directory-or-archive-path>" << std::endl;
    return 1;
  }

  std::string path = argv[1];

  try {
    Traverser traverser({ make_single_path(path) });
    size_t entry_count = 0;
    size_t max_depth = 0;
    std::vector<std::string> relative_paths;

    const std::string root_name = std::filesystem::path(path).filename().string();
    const auto make_relative = [&](const std::string &full_path) {
      if (full_path == path) {
        return root_name;
      }
      const std::string prefix = path + "/";
      if (full_path.rfind(prefix, 0) == 0) {
        return full_path.substr(prefix.size());
      }
      return full_path;
    };

    for (Entry &entry : traverser) {
      ++entry_count;
      max_depth = std::max(max_depth, entry.depth());
      const std::string full_path = hierarchy_display(entry.path_hierarchy());
      relative_paths.push_back(make_relative(full_path));
    }

    const std::string filename = std::filesystem::path(path).filename().string();
    struct ExpectedSummary {
      size_t entries;
      size_t max_depth;
      std::vector<std::string> relative_paths;
    };

    const std::unordered_map<std::string, ExpectedSummary> expectations = {
      {
          "deeply_nested.tar.gz",
          {
              11,
              5,
              {
                  "deeply_nested.tar.gz",
                  "level1.tar.gz",
                  "level1.tar.gz/level2.tar.gz",
                  "level1.tar.gz/level2.tar.gz/level3.tar.gz",
                  "level1.tar.gz/level2.tar.gz/level3.tar.gz/deep.txt",
                  "level1.tar.gz/level2.tar.gz/level3.tar.gz/deep.txt/deep",
                  "level1.tar.gz/level2.tar.gz/archive.tar.gz.part001",
                  "level1.tar.gz/level2.tar.gz/archive.tar.gz.part002",
                  "level1.tar.gz/level2.tar.gz/archive.tar.gz.part003",
                  "level1.tar.gz/root.txt",
                  "root.txt",
              },
          },
      },
    };

    auto it = expectations.find(filename);
    if (it != expectations.end()) {
      const auto &expected = it->second;
      if (entry_count != expected.entries) {
        std::cerr << "Entry count mismatch for " << filename << ": expected " << expected.entries << ", got " << entry_count << std::endl;
        return 1;
      }
      if (max_depth != expected.max_depth) {
        std::cerr << "Max depth mismatch for " << filename << ": expected " << expected.max_depth << ", got " << max_depth << std::endl;
        return 1;
      }
      if (relative_paths != expected.relative_paths) {
        std::cerr << "Traversal order mismatch for " << filename << std::endl;
        return 1;
      }
    } else {
      if (entry_count == 0) {
        std::cerr << "Descent produced no entries for " << path << std::endl;
        return 1;
      }
    }

    const std::unordered_map<std::string, std::vector<std::string>> shallow_expectations = {
      { "deeply_nested.tar.gz", { "deeply_nested.tar.gz" } },
    };

    auto shallow_it = shallow_expectations.find(filename);
    if (shallow_it != shallow_expectations.end()) {
      TraverserOptions shallow_options;
      shallow_options.descend_archives = false;
      Traverser shallow_traverser({ make_single_path(path) }, shallow_options);
      std::vector<std::string> shallow_paths;

      for (Entry &entry : shallow_traverser) {
        const std::string full_path = hierarchy_display(entry.path_hierarchy());
        shallow_paths.push_back(make_relative(full_path));
        if (entry.depth() != 0) {
          std::cerr << "Unexpected depth when descend_archives=false for " << path << std::endl;
          return 1;
        }
      }

      if (shallow_paths != shallow_it->second) {
        std::cerr << "descend_archives=false mismatch for " << filename << std::endl;
        return 1;
      }
    }

    std::cout << "Descent traversal succeeded (entries=" << entry_count << ", max_depth=" << max_depth << ")" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
