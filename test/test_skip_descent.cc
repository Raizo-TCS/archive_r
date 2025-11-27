// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/traverser.h"
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

  try {
    const std::string archive_path = argv[1];
    Traverser traverser({ make_single_path(archive_path) });

    std::vector<std::string> relative_paths;
    size_t total_entries = 0;
    bool descent_disabled = false;
    bool deep_entry_seen = false;

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
      ++total_entries;
      const std::string path = entry.path();
      relative_paths.push_back(make_relative(path));

      if (path.find("level3.tar.gz") != std::string::npos) {
        entry.set_descent(false);
        descent_disabled = true;
      }
      if (path.find("deep.txt") != std::string::npos) {
        deep_entry_seen = true;
      }
    }

    const std::string filename = std::filesystem::path(archive_path).filename().string();
    struct ExpectedOutcome {
      size_t entries;
      std::vector<std::string> relative_paths;
    };

    const std::unordered_map<std::string, ExpectedOutcome> expectations = {
      {
          "deeply_nested.tar.gz",
          {
              9,
              {
                  "deeply_nested.tar.gz",
                  "level1.tar.gz",
                  "level1.tar.gz/level2.tar.gz",
                  "level1.tar.gz/level2.tar.gz/level3.tar.gz",
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
      if (total_entries != expected.entries) {
        std::cerr << "Entry count mismatch for " << filename << ": expected " << expected.entries << ", got " << total_entries << std::endl;
        return 1;
      }
      if (relative_paths != expected.relative_paths) {
        std::cerr << "Traversal order mismatch for " << filename << std::endl;
        return 1;
      }
    } else {
      if (total_entries == 0) {
        std::cerr << "set_descent(false) traversal produced no entries for " << archive_path << std::endl;
        return 1;
      }
    }

    if (!descent_disabled) {
      std::cerr << "set_descent(false) was never triggered for " << archive_path << std::endl;
      return 1;
    }
    if (deep_entry_seen) {
      std::cerr << "Entries containing 'deep.txt' were encountered despite set_descent(false)" << std::endl;
      return 1;
    }

    std::cout << "set_descent(false) behavior succeeded (entries=" << total_entries << ")" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
