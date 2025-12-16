// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/path_hierarchy_utils.h"

#include <algorithm>
#include <filesystem>

namespace archive_r {

namespace {

struct CollapseSegments {
  PathHierarchy prefix;
  PathHierarchy suffix;
  std::size_t multi_volume_depth = 0;
};

PathHierarchy hierarchy_suffix_from(const PathHierarchy &hierarchy, std::size_t start_index) {
  if (hierarchy.empty() || start_index >= hierarchy.size()) {
    return {};
  }
  const auto begin = hierarchy.begin() + static_cast<PathHierarchy::difference_type>(start_index);
  return PathHierarchy(begin, hierarchy.end());
}

bool determine_multi_volume_segments(const std::vector<PathHierarchy> &sources, CollapseSegments &segments) {
  if (sources.empty()) {
    return false;
  }

  const PathHierarchy &reference = sources.front();
  if (reference.empty()) {
    return false;
  }

  std::size_t multi_volume_depth = reference.size() - 1;
  bool difference_found = false;

  for (std::size_t depth = 0; depth < reference.size(); ++depth) {
    const PathEntry &reference_entry = reference[depth];
    for (std::size_t i = 1; i < sources.size(); ++i) {
      const PathHierarchy &candidate = sources[i];
      if (candidate.size() <= depth) {
        return false;
      }
      if (!archive_r::entries_equal(reference_entry, candidate[depth])) {
        multi_volume_depth = depth;
        difference_found = true;
        break;
      }
    }
    if (difference_found) {
      break;
    }
  }

  if (!difference_found) {
    for (std::size_t i = 1; i < sources.size(); ++i) {
      if (sources[i].size() != reference.size()) {
        return false;
      }
    }
  }

  segments.multi_volume_depth = multi_volume_depth;
  segments.prefix.clear();
  if (multi_volume_depth > 0) {
    segments.prefix = pathhierarchy_prefix_until(reference, multi_volume_depth - 1);
  }
  segments.suffix = hierarchy_suffix_from(reference, multi_volume_depth + 1);
  return true;
}

bool collect_multi_volume_parts(const std::vector<PathHierarchy> &sources, const CollapseSegments &segments, std::vector<std::string> &parts) {
  if (sources.empty()) {
    return false;
  }

  const std::size_t suffix_size = segments.suffix.size();
  const std::size_t required_size = segments.multi_volume_depth + 1 + suffix_size;

  parts.clear();
  parts.reserve(sources.size());

  for (const auto &hierarchy : sources) {
    if (hierarchy.empty() || hierarchy.size() <= segments.multi_volume_depth) {
      return false;
    }

    if (hierarchy.size() != required_size) {
      return false;
    }

    const PathEntry &component = hierarchy[segments.multi_volume_depth];
    if (!component.is_single()) {
      return false;
    }

    for (std::size_t offset = 0; offset < suffix_size; ++offset) {
      const PathEntry &reference_entry = segments.suffix[offset];
      const PathEntry &candidate_entry = hierarchy[segments.multi_volume_depth + 1 + offset];
      if (!archive_r::entries_equal(reference_entry, candidate_entry)) {
        return false;
      }
    }

    parts.push_back(component.single_value());
  }

  return true;
}

bool flatten_single_entry(const PathEntry &entry, std::string &output) {
  if (entry.is_single()) {
    output = entry.single_value();
    return true;
  }
  return flatten_entry_to_string(entry, output);
}

} // namespace

const std::string *path_entry_component_at(const PathEntry &entry, std::size_t index) {
  if (entry.is_single()) {
    if (index == 0) {
      return &entry.single_value();
    }
    return nullptr;
  }

  if (entry.is_multi_volume()) {
    const auto &parts = entry.multi_volume_parts().values;
    if (index < parts.size()) {
      return &parts[index];
    }
    return nullptr;
  }

  return nullptr;
}

std::size_t pathhierarchy_volume_size(const PathHierarchy &logical) {
  if (logical.empty()) {
    return 0;
  }
  const PathEntry &tail = logical.back();
  if (!tail.is_multi_volume()) {
    return 1;
  }
  const auto &parts = tail.multi_volume_parts().values;
  return parts.size();
}

std::string pathhierarchy_volume_entry_name(const PathHierarchy &logical, std::size_t index) {
  if (logical.empty()) {
    return {};
  }
  const PathEntry &tail = logical.back();
  if (!tail.is_multi_volume()) {
    if (index != 0) {
      return {};
    }

    std::string entry_name;
    if (!flatten_single_entry(tail, entry_name)) {
      return {};
    }
    return entry_name;
  }

  const auto &parts = tail.multi_volume_parts().values;
  if (index >= parts.size()) {
    return {};
  }
  return parts[index];
}

bool pathhierarchy_is_multivolume(const PathHierarchy &hierarchy) {
  if (hierarchy.empty()) {
    return false;
  }
  return hierarchy.back().is_multi_volume();
}

PathHierarchy pathhierarchy_select_single_part(const PathHierarchy &logical, std::size_t index) {
  if (logical.empty()) {
    return {};
  }

  PathHierarchy result = parent_hierarchy(logical);
  append_single(result, pathhierarchy_volume_entry_name(logical, index));
  return result;
}

void sort_hierarchies(std::vector<PathHierarchy> &hierarchies) {
  std::sort(hierarchies.begin(), hierarchies.end(), [](const PathHierarchy &lhs, const PathHierarchy &rhs) { return archive_r::compare_hierarchies(lhs, rhs) < 0; });
}

bool flatten_entry_to_string(const PathEntry &entry, std::string &output) {
  if (entry.is_single()) {
    output = entry.single_value();
    return true;
  }

  return false;
}

bool entry_name_from_component(const PathEntry &entry, std::string &output) {
  if (entry.is_multi_volume()) {
    const auto &parts = entry.multi_volume_parts().values;
    if (parts.empty()) {
      return false;
    }
    output = parts.front();
    return true;
  }

  return flatten_entry_to_string(entry, output);
}

PathHierarchy merge_multi_volume_sources(const std::vector<PathHierarchy> &sources) {
  CollapseSegments segments;
  if (!determine_multi_volume_segments(sources, segments)) {
    return {};
  }

  std::vector<std::string> parts;
  if (!collect_multi_volume_parts(sources, segments, parts)) {
    return {};
  }

  PathHierarchy result = segments.prefix;
  result.reserve(result.size() + 1 + segments.suffix.size());
  result.emplace_back(PathEntry::multi_volume(std::move(parts)));
  result.insert(result.end(), segments.suffix.begin(), segments.suffix.end());
  return result;
}

std::string path_entry_display(const PathEntry &entry) {
  if (entry.is_single()) {
    return entry.single_value();
  }
  if (entry.is_multi_volume()) {
    std::string value = "[";
    bool first = true;
    for (const auto &part : entry.multi_volume_parts().values) {
      if (!first) {
        value.push_back('|');
      }
      value += part;
      first = false;
    }
    value.push_back(']');
    return value;
  }
  return {};
}

std::string hierarchy_display(const PathHierarchy &hierarchy) {
  std::string result;
  bool first = true;
  for (const auto &component : hierarchy) {
    if (!first) {
      result.push_back('/');
    }
    result += path_entry_display(component);
    first = false;
  }
  return result;
}

} // namespace archive_r
