// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/traverser.h"
#include <algorithm>
#include <iostream>
#include <locale.h>
#include <map>
#include <string>
#include <vector>

using namespace archive_r;

// Simple traverse example using Iterator API
void simple_traverse(const std::string &path) {
  std::cout << "=== Traversing: " << path << " ===\n";

  try {
    Traverser traverser({ make_single_path(path) });

    size_t count = 0;
    for (auto &entry : traverser) {
      // Print depth and path
      std::cout << "[depth=" << entry.depth() << "] " << entry.path();

      // Print size and type
      if (entry.is_file()) {
        std::cout << " (file, " << entry.size() << " bytes)";
      } else if (entry.is_directory()) {
        std::cout << " (dir)";
      }
      std::cout << "\n";
      if (entry.depth() > 0) {
        entry.set_descent(false);
      }

      count++;
    }

    std::cout << "\nTotal entries: " << count << "\n";
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
  }
}

// Find entries matching a pattern
void find_entries(const std::string &path, const std::string &pattern) {
  std::cout << "=== Finding entries matching '" << pattern << "' in: " << path << " ===\n";

  try {
    Traverser traverser({ make_single_path(path) });

    std::vector<std::string> matches;

    for (auto it = traverser.begin(); it != traverser.end(); ++it) {
      Entry &entry = *it;
      std::string entry_path = entry.path();

      // Simple substring match
      if (entry_path.find(pattern) != std::string::npos) {
        matches.push_back(entry_path);
      }
    }

    if (matches.empty()) {
      std::cout << "No matches found.\n";
    } else {
      std::cout << "Found " << matches.size() << " matches:\n";
      for (const auto &match : matches) {
        std::cout << "  " << match << "\n";
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
  }
}

// Count files by depth
void count_by_depth(const std::string &path) {
  std::cout << "=== Counting entries by depth in: " << path << " ===\n";

  try {
    Traverser traverser({ make_single_path(path) });

    std::map<size_t, size_t> depth_counts;

    for (auto &entry : traverser) {
      if (entry.depth() > 0) {
        entry.set_descent(false);
      }
      depth_counts[entry.depth()]++;
    }

    std::cout << "Depth distribution:\n";
    for (const auto &[depth, count] : depth_counts) {
      std::cout << "  Depth " << depth << ": " << count << " entries\n";
    }
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << "\n";
  }
}

int main(int argc, char *argv[]) {
  setlocale(LC_ALL, "");

  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <archive_or_directory> [pattern]\n";
    std::cerr << "\nExamples:\n";
    std::cerr << "  " << argv[0] << " archive.tar.gz\n";
    std::cerr << "  " << argv[0] << " /path/to/directory\n";
    std::cerr << "  " << argv[0] << " archive.tar.gz '.txt'\n";
    return 1;
  }

  std::string path = argv[1];

  if (argc >= 3) {
    // Find mode
    std::string pattern = argv[2];
    find_entries(path, pattern);
  } else {
    // Simple traverse mode
    simple_traverse(path);
    std::cout << "\n";
  }

  return 0;
}
