// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include "archive_r/path_hierarchy.h"
#include "archive_r/platform_compat.h"
#include "archive_type.h"
#include "archive_stack_cursor.h"
#include "entry_fault_error.h"
#include "multi_volume_manager.h"
#include <limits>
#include <memory>
#include <string>
#include <unordered_set>

namespace archive_r {

class ArchiveStackOrchestrator {
public:
  explicit ArchiveStackOrchestrator(const ArchiveOption &options = {});
  ArchiveStackOrchestrator(const ArchiveStackOrchestrator &) = delete;
  ArchiveStackOrchestrator &operator=(const ArchiveStackOrchestrator &) = delete;

  ~ArchiveStackOrchestrator();

  void open_root_hierarchy(const PathHierarchy &root_hierarchy);

  bool advance(bool descend_request = true);
  const std::string &current_entryname();

  size_t depth() const;
  PathHierarchy current_entry_hierarchy();
  bool synchronize_to_hierarchy(const PathHierarchy &path_hierarchy);

  StreamArchive *current_archive();
  ssize_t read_head(void *buff, size_t len);

  const std::unordered_set<std::string> &metadata_keys() const { return _archive_options.metadata_keys; }
  const ArchiveOption &options() const { return _archive_options; }

  void mark_entry_as_multi_volume(const PathHierarchy &entry_path, const std::string &base_name, PathEntry::Parts::Ordering ordering = PathEntry::Parts::Ordering::Natural);
  bool descend_pending_multi_volumes();

private:
  ArchiveOption _archive_options;
  ArchiveStackCursor _head;
  MultiVolumeManager _multi_volume_manager;

  void dispatch_fault(EntryFault fault);
};

} // namespace archive_r
