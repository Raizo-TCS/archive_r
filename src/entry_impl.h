// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include "archive_r/entry.h"
#include "archive_stack_orchestrator.h"
#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_set>

namespace archive_r {

// Entry implementation class - internal to Traverser
class Entry::Impl {
public:
  // Unified constructor - receives metadata directly
  // No libarchive dependency
  // Copy constructor and assignment (orchestrator is not copied)
  Impl(const Impl &other);
  Impl &operator=(const Impl &other);

  Impl(const PathHierarchy &hierarchy, std::shared_ptr<ArchiveStackOrchestrator> data_source_orchestrator, bool default_descent);

  std::string name() const;
  const PathHierarchy &path_hierarchy() const;
  bool is_directory() const;
  bool is_file() const;
  uint64_t size() const;
  size_t depth() const;
  void set_descent(bool enabled);
  bool descent_enabled() const;
  void set_multi_volume_group(const std::string &base_name, const MultiVolumeGroupOptions &options);
  ssize_t read(void *buffer, size_t length);
  const EntryMetadataMap &metadata() const;
  const EntryMetadataValue *metadata_value(const std::string &key) const;

private:
  PathHierarchy _path_hierarchy;

  // Metadata only (no libarchive types)
  uint64_t _size;
  mode_t _filetype;
  EntryMetadataMap _metadata;
  bool _descend_enabled = true; // Flag to control automatic descent

  std::shared_ptr<ArchiveStackOrchestrator> _orchestrator;               ///< Active orchestrator (shared with traverser or detached copy)
  bool _shares_traverser_orchestrator = false;

  mutable std::optional<ArchiveOption> _archive_options;

  void emit_fault(const std::string &message, int errno_value = 0) const;
  std::shared_ptr<ArchiveStackOrchestrator> ensure_orchestrator();
};

} // namespace archive_r
