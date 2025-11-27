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

PathHierarchy make_nested_root(const std::string &path) {
  PathEntry::NodeList nodes;
  nodes.emplace_back(PathEntry::single(path));

  PathHierarchy nested;
  nested.emplace_back(PathEntry::nested(std::move(nodes)));
  return nested;
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

    const auto nested_paths = collect_paths({ make_nested_root(archive_string) });

    if (baseline != nested_paths) {
      std::cerr << "Nested root traversal mismatch: expected " << baseline.size() << " entries, got " << nested_paths.size() << std::endl;
      return 1;
    }

    std::cout << "Nested root traversal matched baseline (entries=" << nested_paths.size() << ")" << std::endl;
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Error: " << ex.what() << std::endl;
    return 1;
  }
}
