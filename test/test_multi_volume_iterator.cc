// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/traverser.h"
#include "entry_read_helpers.h"
#include <cctype>
#include <iostream>
#include <locale.h>
#include <string>
#include <vector>

using namespace archive_r;

// Utility function to check if filename is a multi-volume part
bool is_multi_volume_part(const std::string &filename) {
  size_t pos = filename.rfind(".part");
  if (pos == std::string::npos) {
    return false;
  }

  size_t num_start = pos + 5;
  if (num_start >= filename.size()) {
    return false;
  }

  for (size_t i = num_start; i < filename.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(filename[i]))) {
      return false;
    }
  }
  return true;
}

// Extract base name without .partXXX suffix
std::string get_multi_volume_base_name(const std::string &part_name) {
  size_t pos = part_name.rfind(".part");
  if (pos != std::string::npos) {
    return part_name.substr(0, pos);
  }
  return "";
}

bool ends_with(const std::string &value, const std::string &suffix) { return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0; }

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <archive_path>" << std::endl;
    return 1;
  }

  std::string archive_path = argv[1];

  try {
    Traverser traverser({ make_single_path(archive_path) });
    size_t total_entries = 0;
    size_t multi_volume_parts = 0;
    size_t nested_entries = 0;
    bool file1_found = false;
    bool file2_found = false;

    const std::string file1_suffix = "/file1.txt";
    const std::string file2_suffix = "/file2.txt";

    std::string file1_content;
    std::string file2_content;

    for (Entry &entry : traverser) {
      ++total_entries;

      const auto &hier = entry.path_hierarchy();
      if (!hier.empty()) {
        const PathEntry &tail = hier.back();
        if (tail.is_single()) {
          const std::string &filename = tail.single_value();
          if (is_multi_volume_part(filename)) {
            std::string base_name = get_multi_volume_base_name(filename);
            if (!base_name.empty()) {
              ++multi_volume_parts;
              entry.set_multi_volume_group(base_name);
            }
          }
        }
      }

      if (entry.depth() >= 2) {
        ++nested_entries;
      }

      const std::string path = entry.path();
      if (!file1_found && ends_with(path, file1_suffix)) {
        if (!entry.is_file()) {
          std::cerr << "Expected file for " << file1_suffix << ", got non-file entry" << std::endl;
          return 1;
        }
        if (entry.depth() != 2) {
          std::cerr << "Expected depth 2 for file1.txt, got " << entry.depth() << std::endl;
          return 1;
        }
        const auto data = archive_r::test_helpers::read_entry_fully(entry);
        file1_content.assign(data.begin(), data.end());
        file1_found = true;
      } else if (!file2_found && ends_with(path, file2_suffix)) {
        if (!entry.is_file()) {
          std::cerr << "Expected file for " << file2_suffix << ", got non-file entry" << std::endl;
          return 1;
        }
        if (entry.depth() != 2) {
          std::cerr << "Expected depth 2 for file2.txt, got " << entry.depth() << std::endl;
          return 1;
        }
        const auto data = archive_r::test_helpers::read_entry_fully(entry);
        file2_content.assign(data.begin(), data.end());
        file2_found = true;
      }
    }

    if (multi_volume_parts != 3) {
      std::cerr << "Expected 3 multi-volume parts, found " << multi_volume_parts << std::endl;
      return 1;
    }

    if (nested_entries < 2) {
      std::cerr << "Expected to discover entries inside multi-volume archive, but only " << nested_entries << " nested entries were seen" << std::endl;
      return 1;
    }

    if (!file1_found) {
      std::cerr << "Did not find archive.tar.gz/file1.txt" << std::endl;
      return 1;
    }
    if (!file2_found) {
      std::cerr << "Did not find archive.tar.gz/file2.txt" << std::endl;
      return 1;
    }

    if (file1_content != "content1\n") {
      std::cerr << "Unexpected content for file1.txt: '" << file1_content << "'" << std::endl;
      return 1;
    }
    if (file2_content != "content2\n") {
      std::cerr << "Unexpected content for file2.txt: '" << file2_content << "'" << std::endl;
      return 1;
    }

    std::cout << "Multi-volume iterator test passed (entries=" << total_entries << ")" << std::endl;

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
