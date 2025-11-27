// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include "archive_r/path_hierarchy.h"
#include "archive_type.h"

#include <map>
#include <string>
#include <vector>

namespace archive_r {

class MultiVolumeManager {
public:
  struct MultiVolumeGroup {
    std::vector<PathHierarchy> parts;
    std::string base_name;
    PathHierarchy parent_hierarchy;
    PathEntry::Parts::Ordering ordering = PathEntry::Parts::Ordering::Natural;
  };

  using GroupList = std::vector<MultiVolumeGroup>;
  using ParentGroupMap = std::map<PathHierarchy, GroupList, PathHierarchyLess>;

  void mark_entry_as_multi_volume(const PathHierarchy &entry_path, const std::string &base_name,
                                  PathEntry::Parts::Ordering ordering = PathEntry::Parts::Ordering::Natural);

  bool pop_multi_volume_group(const PathHierarchy &current_hierarchy, PathHierarchy &multi_volume_hierarchy);

private:
  MultiVolumeGroup &get_or_create_multi_volume_group(const PathHierarchy &parent_hierarchy, const std::string &base_name,
                                                     PathEntry::Parts::Ordering ordering);

  bool pop_group_for_parent(const PathHierarchy &parent_hierarchy, MultiVolumeGroup &out_group);

  ParentGroupMap _multi_volume_groups;
};

} // namespace archive_r
