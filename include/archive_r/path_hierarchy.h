// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace archive_r {

/**
 * @brief Represents a single component within a logical path hierarchy.
 *
 * A component can be one of three shapes:
 * - single string value (most common)
 * - multi-volume part list (split archives that share a common base name)
 */
class PathEntry {
public:
  struct Parts {
    std::vector<std::string> values;
    enum class Ordering { Natural, Given } ordering = Ordering::Natural;
  };

  PathEntry() = default;

  explicit PathEntry(std::string value)
      : _value(std::move(value)) {}

  explicit PathEntry(Parts parts)
      : _value(std::move(parts)) {}

  static PathEntry single(std::string entry) { return PathEntry(std::move(entry)); }

  static PathEntry multi_volume(std::vector<std::string> entries, Parts::Ordering ordering = Parts::Ordering::Natural) {
    Parts parts{ std::move(entries), ordering };
    if (parts.values.empty()) {
      throw std::invalid_argument("multi-volume parts cannot be empty");
    }
    return PathEntry(std::move(parts));
  }

  bool is_single() const { return std::holds_alternative<std::string>(_value); }
  bool is_multi_volume() const { return std::holds_alternative<Parts>(_value); }
  const std::string &single_value() const { return std::get<std::string>(_value); }
  const Parts &multi_volume_parts() const { return std::get<Parts>(_value); }
  Parts &multi_volume_parts_mut() { return std::get<Parts>(_value); }

private:
  std::variant<std::string, Parts> _value;
};

using PathHierarchy = std::vector<PathEntry>;

/**
 * Compare two entries using the ordering enforced throughout archive_r.
 *
 * Ordering rules:
 * 1. Entry categories are ordered single < multi-volume.
 * 2. Single entries compare by string value.
 * 3. Multi-volume entries first compare their ordering flag (Natural < Given),
 *    then compare corresponding part names lexicographically, finally by list length.
 */
int compare_entries(const PathEntry &lhs, const PathEntry &rhs);

/** Compare complete hierarchies lexicographically using compare_entries on each level. */
int compare_hierarchies(const PathHierarchy &lhs, const PathHierarchy &rhs);

/** Shorthand equality helpers for entries and hierarchies. */
bool entries_equal(const PathEntry &lhs, const PathEntry &rhs);
bool hierarchies_equal(const PathHierarchy &lhs, const PathHierarchy &rhs);

/** Strict-weak-order functor suitable for associative containers. */
struct PathHierarchyLess {
  bool operator()(const PathHierarchy &lhs, const PathHierarchy &rhs) const;
};

/** Build a hierarchy containing a single leaf component. */
PathHierarchy make_single_path(const std::string &root);

/** Append helpers for single and multi-volume components. */
void append_single(PathHierarchy &hierarchy, std::string value);
void append_multi_volume(PathHierarchy &hierarchy, std::vector<std::string> parts, PathEntry::Parts::Ordering ordering = PathEntry::Parts::Ordering::Natural);

/** Extract prefix/slice helpers. */
PathHierarchy pathhierarchy_prefix_until(const PathHierarchy &hierarchy, size_t inclusive_index);
PathHierarchy parent_hierarchy(const PathHierarchy &hierarchy);
} // namespace archive_r
