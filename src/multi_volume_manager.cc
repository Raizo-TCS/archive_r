// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "multi_volume_manager.h"

#include "archive_r/path_hierarchy_utils.h"

#include <algorithm>
#include <utility>

namespace archive_r {

MultiVolumeManager::MultiVolumeGroup &MultiVolumeManager::get_or_create_multi_volume_group(const PathHierarchy &parent_hierarchy,
                                                                                           const std::string &base_name,
                                                                                           PathEntry::Parts::Ordering ordering) {
  GroupList &groups = _multi_volume_groups[parent_hierarchy];
  auto it = std::find_if(groups.begin(), groups.end(), [&](const MultiVolumeGroup &group) { return group.base_name == base_name; });

  if (it == groups.end()) {
    MultiVolumeGroup group;
    group.base_name = base_name;
    group.parent_hierarchy = parent_hierarchy;
    group.ordering = ordering;
    groups.push_back(std::move(group));
    return groups.back();
  }

  MultiVolumeGroup &target = *it;
  if (ordering == PathEntry::Parts::Ordering::Given) {
    target.ordering = PathEntry::Parts::Ordering::Given;
  }
  return target;
}

bool MultiVolumeManager::pop_group_for_parent(const PathHierarchy &parent_hierarchy, MultiVolumeGroup &out_group) {
  ParentGroupMap::iterator it = _multi_volume_groups.find(parent_hierarchy);
  if (it == _multi_volume_groups.end() || it->second.empty()) {
    return false;
  }

  out_group = it->second.front();
  it->second.erase(it->second.begin());
  if (it->second.empty()) {
    _multi_volume_groups.erase(it);
  }

  return true;
}

void MultiVolumeManager::mark_entry_as_multi_volume(const PathHierarchy &entry_path, const std::string &base_name,
                                                    PathEntry::Parts::Ordering ordering) {
  if (entry_path.empty() || pathhierarchy_is_multivolume(entry_path)) {
    return;
  }

  MultiVolumeGroup &target = get_or_create_multi_volume_group(parent_hierarchy(entry_path), base_name, ordering);
  auto &parts = target.parts;
  const bool exists = std::any_of(parts.begin(), parts.end(), [&](const PathHierarchy &existing) {
    return hierarchies_equal(existing, entry_path);
  });
  if (!exists) {
    parts.push_back(entry_path);
  }
}

bool MultiVolumeManager::pop_multi_volume_group(const PathHierarchy &current_hierarchy, PathHierarchy &multi_volume_hierarchy) {
  MultiVolumeGroup group;
  if (!pop_group_for_parent(current_hierarchy, group)) {
    return false;
  }

  if (group.ordering != PathEntry::Parts::Ordering::Given) {
    sort_hierarchies(group.parts);
  }

  multi_volume_hierarchy = merge_multi_volume_sources(group.parts);
  multi_volume_hierarchy.back().multi_volume_parts_mut().ordering = group.ordering;
  return true;
}

} // namespace archive_r
