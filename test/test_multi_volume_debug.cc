// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/traverser.h"
#include "multi_volume_utils.h"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <locale.h>
#include <string>
#include <vector>

using namespace archive_r;

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <multi_volume_archive_part>" << std::endl;
    return 1;
  }

  std::string archive_path = argv[1];

  try {
    Traverser traverser({ make_single_path(archive_path) });

    const bool is_multi_volume = is_multi_volume_filename(archive_path);
    if (!is_multi_volume) {
      std::cerr << "Input is not recognized as multi-volume: " << archive_path << std::endl;
      return 1;
    }

    const std::string base_name = extract_multi_volume_base_name(std::filesystem::path(archive_path).filename().string());
    std::vector<std::string> parts = collect_multi_volume_files(archive_path);
    std::sort(parts.begin(), parts.end());

    size_t entry_count = 0;
    std::vector<std::string> relative_paths;
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
      relative_paths.push_back(make_relative(entry.path()));
    }

    const std::string filename = archive_filename;
    if (filename == "test_input.tar.gz.part00") {
      if (base_name != "test_input.tar.gz") {
        std::cerr << "Base name mismatch for " << filename << std::endl;
        return 1;
      }

      std::vector<std::string> expected_parts;
      const auto base_dir = std::filesystem::path(archive_path).parent_path();
      for (int i = 0; i <= 4; ++i) {
        std::string suffix = "test_input.tar.gz.part0" + std::to_string(i);
        expected_parts.push_back((base_dir / suffix).string());
      }
      if (parts != expected_parts) {
        std::cerr << "Part list mismatch for " << filename << std::endl;
        return 1;
      }

      const std::vector<std::string> expected_relative_paths = { archive_filename, "test_input_content.txt" };
      if (entry_count != expected_relative_paths.size()) {
        std::cerr << "Entry count mismatch for " << filename << ": expected " << expected_relative_paths.size() << ", got " << entry_count << std::endl;
        return 1;
      }
      if (relative_paths != expected_relative_paths) {
        std::cerr << "Traversal output mismatch for " << filename << std::endl;
        return 1;
      }
    } else {
      if (parts.size() < 2) {
        std::cerr << "Detected fewer than two parts for " << archive_path << std::endl;
        return 1;
      }
      if (entry_count == 0) {
        std::cerr << "Traversal produced no entries for " << archive_path << std::endl;
        return 1;
      }
    }

    std::cout << "Multi-volume debug checks passed (parts=" << parts.size() << ", entries=" << entry_count << ")" << std::endl;

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
