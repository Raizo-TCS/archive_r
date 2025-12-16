// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/path_hierarchy.h"

#include <string>
#include <utility>
#include <vector>

namespace archive_r {
namespace {

int entry_type_rank(const PathEntry &entry) {
  if (entry.is_single()) {
    return 0;
  }
  return 1;
}

int compare_entries_impl(const PathEntry &lhs, const PathEntry &rhs) {
  const int lhs_rank = entry_type_rank(lhs);
  const int rhs_rank = entry_type_rank(rhs);
  if (lhs_rank != rhs_rank) {
    return lhs_rank < rhs_rank ? -1 : 1;
  }

  if (lhs.is_single()) {
    const auto &lval = lhs.single_value();
    const auto &rval = rhs.single_value();
    if (lval < rval) {
      return -1;
    }
    if (rval < lval) {
      return 1;
    }
    return 0;
  }

  if (lhs.is_multi_volume()) {
    const auto &lparts = lhs.multi_volume_parts();
    const auto &rparts = rhs.multi_volume_parts();
    if (lparts.ordering != rparts.ordering) {
      return static_cast<int>(lparts.ordering) < static_cast<int>(rparts.ordering) ? -1 : 1;
    }

    const std::size_t lsize = lparts.values.size();
    const std::size_t rsize = rparts.values.size();
    const std::size_t compare_count = lsize < rsize ? lsize : rsize;
    for (std::size_t i = 0; i < compare_count; ++i) {
      const std::string &lvalue = lparts.values[i];
      const std::string &rvalue = rparts.values[i];
      if (lvalue < rvalue) {
        return -1;
      }
      if (rvalue < lvalue) {
        return 1;
      }
    }

    if (lsize != rsize) {
      return lsize < rsize ? -1 : 1;
    }

    return 0;
  }

  return 0;
}

int compare_hierarchies_impl(const PathHierarchy &lhs, const PathHierarchy &rhs) {
  const std::size_t lhs_size = lhs.size();
  const std::size_t rhs_size = rhs.size();
  const std::size_t compare_count = lhs_size < rhs_size ? lhs_size : rhs_size;
  for (std::size_t i = 0; i < compare_count; ++i) {
    const int cmp = compare_entries_impl(lhs[i], rhs[i]);
    if (cmp != 0) {
      return cmp;
    }
  }
  if (lhs_size != rhs_size) {
    return lhs_size < rhs_size ? -1 : 1;
  }
  return 0;
}

bool entries_equal_impl(const PathEntry &lhs, const PathEntry &rhs) { return compare_entries_impl(lhs, rhs) == 0; }

bool hierarchies_equal_impl(const PathHierarchy &lhs, const PathHierarchy &rhs) { return compare_hierarchies_impl(lhs, rhs) == 0; }

} // namespace

int compare_entries(const PathEntry &lhs, const PathEntry &rhs) { return compare_entries_impl(lhs, rhs); }

int compare_hierarchies(const PathHierarchy &lhs, const PathHierarchy &rhs) { return compare_hierarchies_impl(lhs, rhs); }

bool entries_equal(const PathEntry &lhs, const PathEntry &rhs) { return entries_equal_impl(lhs, rhs); }

bool hierarchies_equal(const PathHierarchy &lhs, const PathHierarchy &rhs) { return hierarchies_equal_impl(lhs, rhs); }

bool PathHierarchyLess::operator()(const PathHierarchy &lhs, const PathHierarchy &rhs) const { return compare_hierarchies(lhs, rhs) < 0; }

PathHierarchy make_single_path(const std::string &root) {
  PathHierarchy hierarchy;
  hierarchy.emplace_back(PathEntry::single(root));
  return hierarchy;
}

void append_single(PathHierarchy &hierarchy, std::string value) { hierarchy.emplace_back(PathEntry::single(std::move(value))); }

void append_multi_volume(PathHierarchy &hierarchy, std::vector<std::string> parts, PathEntry::Parts::Ordering ordering) {
  hierarchy.emplace_back(PathEntry::multi_volume(std::move(parts), ordering));
}

PathHierarchy pathhierarchy_prefix_until(const PathHierarchy &hierarchy, size_t inclusive_index) {
  if (hierarchy.empty() || inclusive_index >= hierarchy.size()) {
    return {};
  }
  const auto prefix_end = hierarchy.begin() + static_cast<PathHierarchy::difference_type>(inclusive_index + 1);
  return PathHierarchy(hierarchy.begin(), prefix_end);
}

PathHierarchy parent_hierarchy(const PathHierarchy &hierarchy) {
  if (hierarchy.size() <= 1) {
    return {};
  }
  return pathhierarchy_prefix_until(hierarchy, hierarchy.size() - 2);
}


} // namespace archive_r
