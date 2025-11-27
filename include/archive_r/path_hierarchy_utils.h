// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include "archive_r/path_hierarchy.h"
#include <cstddef>
#include <string>
#include <vector>

namespace archive_r {

/** Return pointer to the Nth single value of an entry (nullptr if absent). */
const std::string *path_entry_component_at(const PathEntry &entry, std::size_t index);

/** Convenience helpers for multi-volume PathHierarchy nodes. */
std::size_t pathhierarchy_volume_size(const PathHierarchy &logical);
std::string pathhierarchy_volume_entry_name(const PathHierarchy &logical, std::size_t index);
bool pathhierarchy_is_multivolume(const PathHierarchy &hierarchy);
PathHierarchy pathhierarchy_select_single_part(const PathHierarchy &logical, std::size_t index);

/** Combine sibling hierarchies that differ only by their terminal part list. */
PathHierarchy merge_multi_volume_sources(const std::vector<PathHierarchy> &sources);

/** Sort hierarchies using PathHierarchyLess semantics. */
void sort_hierarchies(std::vector<PathHierarchy> &hierarchies);

/** Render helpers converting entries to flattened strings for diagnostics. */
bool flatten_entry_to_string(const PathEntry &entry, std::string &output);
bool entry_name_from_component(const PathEntry &entry, std::string &output);

/** Human readable pretty-printers used in logging and debug output. */
std::string path_entry_display(const PathEntry &entry);
std::string hierarchy_display(const PathHierarchy &hierarchy);


} // namespace archive_r
