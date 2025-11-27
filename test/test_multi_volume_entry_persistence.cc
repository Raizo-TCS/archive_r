// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"
#include "entry_read_helpers.h"

#include <cctype>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace archive_r;

namespace {

bool is_multi_volume_part(const std::string &filename) {
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

std::string get_multi_volume_base_name(const std::string &filename) {
  const size_t pos = filename.rfind(".part");
  if (pos == std::string::npos) {
    return {};
  }
  return filename.substr(0, pos);
}

std::string filename_from_path(const std::string &path) {
  const size_t pos = path.rfind('/');
  if (pos == std::string::npos) {
    return path;
  }
  return path.substr(pos + 1);
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
  const std::string archive_path = "test_data/multi_volume_test.tar.gz";
  const std::string target_multi_volume_base = "archive.tar.gz";
  const std::string target_filename = "file1.txt";

  try {
    Traverser traverser({ make_single_path(archive_path) });
    bool group_marked = false;
    bool target_multi_volume_detected = false;
    std::optional<Entry> saved_entry;
    size_t saved_entry_depth = 0;

    for (Entry &entry : traverser) {
      const auto &hierarchy = entry.path_hierarchy();
      const std::string full_path = hierarchy_display(hierarchy);
      if (!hierarchy.empty()) {
        const PathEntry &tail = hierarchy.back();
        if (tail.is_single()) {
          const std::string &filename = tail.single_value();
          if (is_multi_volume_part(filename)) {
            const std::string base_name = get_multi_volume_base_name(filename);
            if (!base_name.empty()) {
              entry.set_multi_volume_group(base_name);
              group_marked = true;
              if (base_name == target_multi_volume_base) {
                target_multi_volume_detected = true;
              }
            }
          }
        }
      }

      if (!saved_entry.has_value() && entry.is_file() && target_multi_volume_detected && !hierarchy.empty()) {
        const PathEntry &tail = hierarchy.back();
        if (tail.is_single() && tail.single_value() == target_filename && hierarchy_contains_target_group(hierarchy, target_multi_volume_base)) {
          saved_entry = entry; // Copy entry while traversal is active
          saved_entry_depth = entry.depth();
        }
      }
    }

    if (!group_marked) {
      std::cerr << "Multi-volume parts were not detected/marked" << std::endl;
      return 1;
    }

    if (!target_multi_volume_detected) {
      std::cerr << "Target multi-volume group '" << target_multi_volume_base << "' was not detected" << std::endl;
      return 1;
    }

    if (!saved_entry.has_value()) {
      std::cerr << "Target entry inside multi-volume archive was not captured" << std::endl;
      return 1;
    }

    if (saved_entry_depth < 2) {
      std::cerr << "Captured entry depth was " << saved_entry_depth << ", expected to be >= 2" << std::endl;
      return 1;
    }

    std::vector<uint8_t> data = archive_r::test_helpers::read_entry_fully(*saved_entry);
    const std::string content(data.begin(), data.end());
    const std::string expected_content = "content1\n";
    if (content != expected_content) {
      std::cerr << "Unexpected content after traversal: '" << content << "'" << std::endl;
      return 1;
    }

    std::cout << "✓ Multi-volume entry persistence test passed" << std::endl;
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Exception: " << ex.what() << std::endl;
    return 1;
  }
}
