// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/path_hierarchy.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace archive_r;

namespace {

std::vector<std::string> collect_paths(const std::vector<PathHierarchy> &roots) {
  Traverser traverser(roots);
  std::vector<std::string> collected;
  for (Entry &entry : traverser) {
    collected.push_back(hierarchy_display(entry.path_hierarchy()));
  }
  return collected;
}

std::vector<std::string> collect_paths(const std::string &root) {
  Traverser traverser(root);
  std::vector<std::string> collected;
  for (Entry &entry : traverser) {
    collected.push_back(hierarchy_display(entry.path_hierarchy()));
  }
  return collected;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <archive_path>" << std::endl;
    return 1;
  }

  const std::filesystem::path archive_path = std::filesystem::path(argv[1]).lexically_normal();
  const std::string archive_string = archive_path.string();

  try {
    const auto baseline = collect_paths({ make_single_path(archive_string) });
    if (baseline.empty()) {
      std::cerr << "Baseline traversal produced no entries for " << archive_string << std::endl;
      return 1;
    }

    const auto direct_paths = collect_paths(archive_string);

    if (baseline != direct_paths) {
      std::cerr << "Direct root traversal mismatch: expected " << baseline.size() << " entries, got " << direct_paths.size() << std::endl;
      return 1;
    }

    std::cout << "Direct root traversal matched baseline (entries=" << direct_paths.size() << ")" << std::endl;
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Error: " << ex.what() << std::endl;
    return 1;
  }
}
