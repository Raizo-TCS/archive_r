// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"

#include <cctype>
#include <iostream>
#include <string>
#include <vector>

using namespace archive_r;

namespace {

std::string filename_from_path(const std::string &path) {
  const size_t pos = path.rfind('/');
  if (pos == std::string::npos) {
    return path;
  }
  return path.substr(pos + 1);
}

bool is_multi_volume_part(const std::string &path) {
  const std::string filename = filename_from_path(path);
  const size_t pos = filename.rfind(".part");
  if (pos == std::string::npos) {
    return false;
  }
  if (pos + 5 >= filename.size()) {
    return false;
  }
  for (size_t i = pos + 5; i < filename.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(filename[i]))) {
      return false;
    }
  }
  return true;
}

std::string get_multi_volume_base_name(const std::string &path) {
  const std::string filename = filename_from_path(path);
  const size_t pos = filename.rfind(".part");
  if (pos == std::string::npos) {
    return {};
  }
  return filename.substr(0, pos);
}

bool hierarchy_contains_target_group(const PathHierarchy &hierarchy, const std::string &target_base_name) {
  if (target_base_name.empty()) {
    return false;
  }
  for (const PathEntry &component : hierarchy) {
    if (!component.is_multi_volume()) {
      continue;
    }
    const auto &parts = component.multi_volume_parts().values;
    for (const std::string &part_path : parts) {
      const std::string part_filename = filename_from_path(part_path);
      if (!is_multi_volume_part(part_filename)) {
        continue;
      }
      const std::string base = get_multi_volume_base_name(part_filename);
      if (base == target_base_name) {
        return true;
      }
    }
  }
  return false;
}

} // namespace

int main() {
  const std::string warm_up_archive = "test_data/deeply_nested.tar.gz";
  const std::vector<std::string> level1_parts = {
    "test_data/level1.tar.part001", "test_data/level1.tar.part002", "test_data/level1.tar.part003", "test_data/level1.tar.part004",
    "test_data/level1.tar.part005", "test_data/level1.tar.part006", "test_data/level1.tar.part007",
  };

  std::vector<PathHierarchy> paths;
  paths.push_back(make_single_path(warm_up_archive));
  for (const std::string &part_path : level1_parts) {
    paths.push_back(make_single_path(part_path));
  }

  const std::string target_multi_volume_base = "level1.tar";

  try {
    Traverser traverser(paths);
    size_t root_part_entries = 0;
    bool aggregated_entry_seen = false;

    for (Entry &entry : traverser) {
      const std::string entry_name = entry.name();
      if (is_multi_volume_part(entry_name)) {
        const std::string base_name = get_multi_volume_base_name(entry_name);
        if (base_name == target_multi_volume_base) {
          entry.set_multi_volume_group(base_name);
          entry.set_descent(false);
          ++root_part_entries;
          continue;
        }
      }

      if (hierarchy_contains_target_group(entry.path_hierarchy(), target_multi_volume_base)) {
        aggregated_entry_seen = true;
        break;
      }
    }

    if (root_part_entries == 0) {
      std::cerr << "Root-level multi-volume parts were not visited" << std::endl;
      return 1;
    }

    if (!aggregated_entry_seen) {
      std::cerr << "Combined multi-volume archive was not activated" << std::endl;
      return 1;
    }

    std::cout << "Root-level multi-volume activation verified" << std::endl;
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Exception: " << ex.what() << std::endl;
    return 1;
  }
}
