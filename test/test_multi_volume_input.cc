// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/traverser.h"
#include "entry_read_helpers.h"
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <locale.h>
#include <string>
#include <unordered_map>
#include <vector>

using namespace archive_r;

// Helper function to check if an entry name looks like a multi-volume file
bool is_multi_volume_part(const std::string &name) {
  size_t len = name.length();
  if (len < 6)
    return false;

  // Check for patterns like .part001, .part01, .001, .01
  if (name.substr(len - 7, 5) == ".part" && std::isdigit(name[len - 2]) && std::isdigit(name[len - 1])) {
    return true;
  }
  if (name[len - 4] == '.' && std::isdigit(name[len - 3]) && std::isdigit(name[len - 2]) && std::isdigit(name[len - 1])) {
    return true;
  }
  return false;
}

// Extract base name from multi-volume filename
std::string extract_base_name(const std::string &name) {
  size_t len = name.length();

  if (name.substr(len - 7, 5) == ".part") {
    return name.substr(0, len - 8); // Remove .partXXX
  }
  if (name[len - 4] == '.') {
    return name.substr(0, len - 4); // Remove .XXX
  }
  return name;
}

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <multi_volume_archive_part>" << std::endl;
    std::cerr << "Example: " << argv[0] << " archive.tar.gz.part001" << std::endl;
    return 1;
  }

  std::string archive_path = argv[1];

  try {
    Traverser traverser({ make_single_path(archive_path) });

    size_t entry_count = 0;
    size_t max_depth = 0;
    std::vector<std::string> relative_paths;
    std::vector<uint8_t> captured_content;

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
      ++entry_count;
      size_t depth = entry.depth();
      if (depth > max_depth) {
        max_depth = depth;
      }
      relative_paths.push_back(make_relative(entry.path()));

      if (captured_content.empty() && entry.is_file() && entry.depth() > 0) {
        captured_content = archive_r::test_helpers::read_entry_fully(entry);
      }

      std::string path = entry.path();
      if (is_multi_volume_part(path)) {
        std::string base_name = extract_base_name(path);
        entry.set_multi_volume_group(base_name);
      }
    }

    const std::string filename = std::filesystem::path(archive_path).filename().string();
    struct ExpectedOutcome {
      size_t entries;
      size_t max_depth;
      std::string content_prefix;
    };

    const std::unordered_map<std::string, ExpectedOutcome> expectations = {
      {
          "test_input.tar.gz.part00",
          {
              2,
              1,
              "VWXYZABCDEFGHIJKLMNO",
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
      const auto report_paths = [&]() {
        std::cerr << "Observed relative paths:" << std::endl;
        for (const auto &value : relative_paths) {
          std::cerr << "  - " << value << std::endl;
        }
      };
      if (relative_paths.size() != 2) {
        report_paths();
        std::cerr << "Traversal order mismatch for " << filename << std::endl;
        return 1;
      }
      if (relative_paths[0] != archive_filename) {
        report_paths();
        std::cerr << "Traversal order mismatch for " << filename << std::endl;
        return 1;
      }
      const std::string inner_suffix = "test_input_content.txt";
      const std::string &inner_path = relative_paths[1];
      if (inner_path != inner_suffix) {
        const std::string decorated_suffix = "/" + inner_suffix;
        if (inner_path.size() <= decorated_suffix.size() || inner_path.compare(inner_path.size() - decorated_suffix.size(), decorated_suffix.size(), decorated_suffix) != 0 || inner_path.front() != '[') {
          report_paths();
          std::cerr << "Traversal order mismatch for " << filename << std::endl;
          return 1;
        }
      }
      if (captured_content.size() < expected.content_prefix.size()) {
        std::cerr << "Captured content shorter than expected prefix for " << filename << std::endl;
        return 1;
      }
      const std::string content_prefix(captured_content.begin(), captured_content.begin() + expected.content_prefix.size());
      if (content_prefix != expected.content_prefix) {
        std::cerr << "Content prefix mismatch for " << filename << ": expected '" << expected.content_prefix << "', got '" << content_prefix << "'" << std::endl;
        return 1;
      }
    } else {
      if (entry_count == 0) {
        std::cerr << "Multi-volume traversal produced no entries for " << archive_path << std::endl;
        return 1;
      }
    }

    std::cout << "Multi-volume input traversal succeeded (entries=" << entry_count << ", max_depth=" << max_depth << ")" << std::endl;

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
