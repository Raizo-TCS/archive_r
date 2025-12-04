// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_stack_orchestrator.h"
#include "entry_fault_error.h"
#include "entry_impl.h"
#include "system_file_stream.h"
#include <archive.h>
#include <archive_entry.h>
#include <filesystem>
#include <sstream>
#include <sys/stat.h>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace archive_r {

// ============================================================================
// Entry::Impl Implementation
// ============================================================================

// Unified constructor - receives metadata directly
// Copy constructor - cached orchestrator is NOT copied (will be recreated on first read)
Entry::Impl::Impl(const Impl &other)
    : _path_hierarchy(other._path_hierarchy)
    , _size(other._size)
    , _filetype(other._filetype)
    , _metadata(other._metadata)
    , _descend_enabled(other._descend_enabled)
    , _orchestrator(nullptr)
    , _shares_traverser_orchestrator(false)
    , _archive_options(other._archive_options) {
}

// Copy assignment operator
Entry::Impl &Entry::Impl::operator=(const Impl &other) {
  if (this != &other) {
    _path_hierarchy = other._path_hierarchy;
    _size = other._size;
    _filetype = other._filetype;
    _metadata = other._metadata;
    _descend_enabled = other._descend_enabled;
    _orchestrator.reset();
    _shares_traverser_orchestrator = false;
    _archive_options = other._archive_options;
  }
  return *this;
}

Entry::Impl::Impl(const PathHierarchy &hierarchy, std::shared_ptr<ArchiveStackOrchestrator> data_source_orchestrator, bool default_descent)
    : _path_hierarchy(hierarchy)
    , _size(0)
    , _filetype(0)
    , _descend_enabled(default_descent)
    , _orchestrator(std::move(data_source_orchestrator))
    , _shares_traverser_orchestrator(static_cast<bool>(_orchestrator)) {
  if (!_orchestrator) {
    return;
  }

  const auto &keys = _orchestrator->metadata_keys();

  const StreamArchive *archive = _orchestrator->current_archive();
  const bool use_archive_metadata = archive && _path_hierarchy.size() > 1;

  if (use_archive_metadata) {
    _size = archive->current_entry_size();
    _filetype = archive->current_entry_filetype();
    if (!keys.empty()) {
      _metadata = archive->current_entry_metadata(keys);
    }
  } else if (!hierarchy.empty()) {
    FilesystemMetadataInfo info = collect_root_path_metadata(hierarchy, keys);
    _size = info.size;
    _filetype = info.filetype;
    _metadata = std::move(info.metadata);
  }

  if (use_archive_metadata) {
    _archive_options = _orchestrator->options();
  } else {
    _archive_options.reset();
  }

  if (_filetype == 0 && archive && _orchestrator->depth() == _path_hierarchy.size()) {
    _filetype = AE_IFREG;
  }
}

std::string Entry::Impl::name() const {
  if (_path_hierarchy.empty()) {
    return "";
  }
  const PathEntry &tail = _path_hierarchy.back();
  std::string display_name;
  if (entry_name_from_component(tail, display_name) && !display_name.empty()) {
    return display_name;
  }
  if (tail.is_single()) {
    return tail.single_value();
  }
  return path_entry_display(tail);
}

const PathHierarchy &Entry::Impl::path_hierarchy() const { return _path_hierarchy; }

bool Entry::Impl::is_directory() const { return _filetype == AE_IFDIR; }

bool Entry::Impl::is_file() const { return _filetype == AE_IFREG; }

uint64_t Entry::Impl::size() const { return _size; }

size_t Entry::Impl::depth() const {
  // Depth is based on path hierarchy length
  return _path_hierarchy.size() > 0 ? _path_hierarchy.size() - 1 : 0;
}

void Entry::Impl::set_descent(bool enabled) {
  if (!_shares_traverser_orchestrator) {
    emit_fault("set_descent requires traverser-managed orchestrator");
    return;
  }
  _descend_enabled = enabled;
}

bool Entry::Impl::descent_enabled() const { return _descend_enabled; }

const EntryMetadataMap &Entry::Impl::metadata() const { return _metadata; }

const EntryMetadataValue *Entry::Impl::metadata_value(const std::string &key) const {
  const auto it = _metadata.find(key);
  if (it == _metadata.end()) {
    return nullptr;
  }
  return &it->second;
}

void Entry::Impl::set_multi_volume_group(const std::string &base_name, const MultiVolumeGroupOptions &options) {
  if (!_shares_traverser_orchestrator) {
    emit_fault("set_multi_volume_group requires traverser-managed orchestrator");
    return;
  }

  // Notify traverser via ArchiveStackOrchestrator
  _descend_enabled = false;
  _orchestrator->mark_entry_as_multi_volume(_path_hierarchy, base_name, options.ordering);
}

void Entry::Impl::emit_fault(const std::string &message, int errno_value) const {
  EntryFault fault;
  fault.message = message;
  fault.errno_value = errno_value;
  fault.hierarchy = _path_hierarchy;
  dispatch_registered_fault(fault);
}

std::shared_ptr<ArchiveStackOrchestrator> Entry::Impl::ensure_orchestrator() {
  if (_orchestrator) {
    if (_shares_traverser_orchestrator && _orchestrator->depth() == 0) {
      // Traverser-managed orchestrator never synchronized to this entry (depth 0 filesystem read).
      _orchestrator.reset();
      _shares_traverser_orchestrator = false;
    } else {
      return _orchestrator;
    }
  }

  ArchiveOption opts = _archive_options.value_or(ArchiveOption{});
  _orchestrator = std::make_shared<ArchiveStackOrchestrator>(opts);
  if (!_orchestrator->synchronize_to_hierarchy(_path_hierarchy)) {
    _orchestrator.reset();
    return nullptr;
  }
  _shares_traverser_orchestrator = false;
  return _orchestrator;
}

ssize_t Entry::Impl::read(void *buffer, size_t length) {
  if (!ensure_orchestrator()) {
    emit_fault("Failed to initialize ArchiveStackOrchestrator");
    return -1;
  }
  const ssize_t bytes_read = _orchestrator->read_head(buffer, length);
  if (bytes_read < 0) {
    emit_fault("Failed to read entry content");
    return -1;
  }

  _descend_enabled = false; // Require explicit re-enable before descending again

  return bytes_read;
}

// ============================================================================
// Entry Public API Implementation
// ============================================================================

Entry::Entry(const PathHierarchy &hierarchy, std::shared_ptr<ArchiveStackOrchestrator> data_source_orchestrator, bool default_descent)
  : _impl(std::make_unique<Impl>(hierarchy, std::move(data_source_orchestrator), default_descent)) {}

Entry::Entry(Impl *impl)
    : _impl(impl) {}

Entry::~Entry() = default;

std::unique_ptr<Entry> Entry::create(PathHierarchy hierarchy, std::shared_ptr<ArchiveStackOrchestrator> data_source_orchestrator, bool default_descent) {
  return std::unique_ptr<Entry>(new Entry(hierarchy, std::move(data_source_orchestrator), default_descent));
}

// Copy operations - creates a new Impl copy
Entry::Entry(const Entry &other)
    : _impl(other._impl ? std::make_unique<Impl>(*other._impl) : nullptr) {}

Entry &Entry::operator=(const Entry &other) {
  if (this != &other) {
    _impl = other._impl ? std::make_unique<Impl>(*other._impl) : nullptr;
  }
  return *this;
}

Entry::Entry(Entry &&) noexcept = default;
Entry &Entry::operator=(Entry &&) noexcept = default;

std::string Entry::name() const { return _impl->name(); }

std::string Entry::path() const { return hierarchy_display(_impl->path_hierarchy()); }

const PathHierarchy &Entry::path_hierarchy() const { return _impl->path_hierarchy(); }

bool Entry::is_directory() const { return _impl->is_directory(); }

bool Entry::is_file() const { return _impl->is_file(); }

uint64_t Entry::size() const { return _impl->size(); }

size_t Entry::depth() const { return _impl->depth(); }

ssize_t Entry::read(void *buffer, size_t length) { return _impl->read(buffer, length); }

void Entry::set_descent(bool enabled) { _impl->set_descent(enabled); }

bool Entry::descent_enabled() const { return _impl->descent_enabled(); }

void Entry::set_multi_volume_group(const std::string &base_name, const MultiVolumeGroupOptions &options) { _impl->set_multi_volume_group(base_name, options); }

const EntryMetadataMap &Entry::metadata() const { return _impl->metadata(); }

const EntryMetadataValue *Entry::find_metadata(const std::string &key) const { return _impl->metadata_value(key); }

} // namespace archive_r
