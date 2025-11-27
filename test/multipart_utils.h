// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include <algorithm>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

namespace archive_r {

// Check if a filename appears to be a multi-volume archive part
inline bool is_multi_volume_filename(const std::string &filename) {
  // Match patterns like: .part001, .part01, .001, .01, etc.
  static const std::regex part_pattern(R"(\.(part)?\d{2,3}$)", std::regex::icase);
  return std::regex_search(filename, part_pattern);
}

// Extract base name from a multi-volume filename
// e.g., "archive.tar.gz.part001" -> "archive.tar.gz"
//       "archive.tar.001" -> "archive.tar"
inline std::string extract_multi_volume_base_name(const std::string &filename) {
  static const std::regex part_pattern(R"(\.(part)?\d{2,3}$)", std::regex::icase);
  return std::regex_replace(filename, part_pattern, "");
}

// Collect all parts of a multipart archive
// Given one part (e.g., "archive.tar.gz.part001"), find all related parts
inline std::vector<std::string> collect_multi_volume_files(const std::string &filepath) {
  namespace fs = std::filesystem;

  if (!is_multi_volume_filename(filepath)) {
    // Not a multi-volume file
    return {};
  }

  fs::path path(filepath);
  if (!fs::exists(path)) {
    throw std::runtime_error("File does not exist: " + filepath);
  }

  std::string filename = path.filename().string();
  std::string base_name = extract_multi_volume_base_name(filename);
  fs::path dir = path.parent_path();
  if (dir.empty()) {
    dir = ".";
  }

  // Find all files in the same directory with the same base name
  std::vector<std::string> parts;

  // Pattern to match: basename + optional .part + digits
  std::string pattern_str = base_name;
  // Escape special regex characters in base_name
  pattern_str = std::regex_replace(pattern_str, std::regex(R"([\.\^\$\|\(\)\[\]\{\}\*\+\?\\])"), R"(\$&)");
  pattern_str += R"(\.(part)?\d{2,3}$)";
  std::regex parts_pattern(pattern_str, std::regex::icase);

  for (const auto &entry : fs::directory_iterator(dir)) {
    if (entry.is_regular_file()) {
      std::string entry_filename = entry.path().filename().string();
      if (std::regex_search(entry_filename, parts_pattern)) {
        parts.push_back(entry.path().string());
      }
    }
  }

  // Sort parts by filename to ensure correct order
  std::sort(parts.begin(), parts.end());

  if (parts.empty()) {
    throw std::runtime_error("No multi-volume files found for: " + filepath);
  }

  return parts;
}

} // namespace archive_r
