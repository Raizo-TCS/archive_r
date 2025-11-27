// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/traverser.h"
#include "archive_r/path_hierarchy_utils.h"
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace archive_r;

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <archive_path> <directory_path>" << std::endl;
    return 1;
  }

  std::vector<std::string> root_strings;
  std::vector<PathHierarchy> root_hierarchies;
  for (int i = 1; i < argc; ++i) {
    root_strings.emplace_back(argv[i]);
    root_hierarchies.emplace_back(make_single_path(argv[i]));
  }

  try {
    TraverserOptions options;
    options.formats = { "7zip", "ar", "cab", "cpio", "empty", "iso9660", "lha", "mtree", "rar", "tar", "warc", "xar", "zip" }; // omit raw to avoid treating regular files as archives

    Traverser traverser(root_hierarchies, options);
    std::unordered_map<std::string, size_t> counts;
    size_t total_entries = 0;

    for (Entry &entry : traverser) {
      ++total_entries;
      const auto &hierarchy = entry.path_hierarchy();
      if (hierarchy.empty()) {
        std::cerr << "Encountered entry without hierarchy information" << std::endl;
        return 1;
      }

      const std::string first_component = path_entry_display(hierarchy.front());
      const std::string *matched_root = nullptr;
      for (const auto &root : root_strings) {
        if (first_component == root) {
          matched_root = &root;
          break;
        }
        if (first_component.rfind(root + '/', 0) == 0) {
          matched_root = &root;
          break;
        }
      }

      if (!matched_root) {
        std::cerr << "Could not associate entry with a root: " << first_component << std::endl;
        return 1;
      }

      counts[*matched_root] += 1;
    }

    if (counts.size() != root_strings.size()) {
      std::cerr << "Traversal did not produce entries for all roots" << std::endl;
      for (const auto &root : root_strings) {
        if (counts.find(root) == counts.end()) {
          std::cerr << "  Missing entries for: " << root << std::endl;
        }
      }
      return 1;
    }

  const size_t expected_archive_entries = 11;
  const size_t expected_directory_entries = 10;
    const size_t expected_total = expected_archive_entries + expected_directory_entries;

    if (total_entries != expected_total) {
      std::cerr << "Total entry count mismatch: expected " << expected_total << ", got " << total_entries << std::endl;
      return 1;
    }

    if (counts[root_strings[0]] != expected_archive_entries) {
      std::cerr << "Entry count mismatch for first root (" << root_strings[0] << "): expected " << expected_archive_entries << ", got " << counts[root_strings[0]] << std::endl;
      return 1;
    }

    if (counts[root_strings[1]] != expected_directory_entries) {
      std::cerr << "Entry count mismatch for second root (" << root_strings[1] << "): expected " << expected_directory_entries << ", got " << counts[root_strings[1]] << std::endl;
      return 1;
    }

    std::cout << "Multi-root traversal succeeded (archive=" << counts[root_strings[0]] << ", directory=" << counts[root_strings[1]] << ", total=" << total_entries << ")" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
