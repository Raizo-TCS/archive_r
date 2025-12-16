// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/traverser.h"
#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/entry_fault.h"
#include "archive_stack_orchestrator.h"
#include "archive_type.h"
#include "entry_fault_error.h"
#include "system_file_stream.h"
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace archive_r {

// ============================================================================
// Traverser Implementation
// ============================================================================

namespace {

archive_r::ArchiveOption to_archive_option(const TraverserOptions &options) {
  archive_r::ArchiveOption converted;
  converted.passphrases = options.passphrases;
  converted.formats = options.formats;
  converted.metadata_keys.insert(options.metadata_keys.begin(), options.metadata_keys.end());
  return converted;
}

} // namespace

Traverser::Traverser(std::vector<PathHierarchy> paths, TraverserOptions options)
    : _initial_paths(std::move(paths))
    , _options(std::move(options)) {
  if (_initial_paths.empty()) {
    throw std::invalid_argument("paths cannot be empty");
  }
  for (const auto &hierarchy : _initial_paths) {
    if (hierarchy.empty()) {
      throw std::invalid_argument("path hierarchy cannot be empty");
    }
  }

}

Traverser::Traverser(PathHierarchy path, TraverserOptions options)
    : Traverser(std::vector<PathHierarchy>{std::move(path)}, std::move(options)) {}

Traverser::Traverser(const std::string &path, TraverserOptions options)
    : Traverser(std::vector<PathHierarchy>{make_single_path(path)}, std::move(options)) {}

Traverser::~Traverser() = default;

// ============================================================================
// Iterator Implementation
// ============================================================================

class Traverser::Iterator::Impl {
public:
    Impl(std::vector<PathHierarchy> paths, bool at_end, const TraverserOptions &traverser_options)
      : _paths(std::move(paths))
      , _at_end(at_end)
      , _archive_options(to_archive_option(traverser_options))
      , _default_descent(traverser_options.descend_archives) {
    if (_at_end) {
      return;
    }
    if (_paths.empty()) {
      throw std::invalid_argument("paths cannot be empty");
    }
    ensure_shared_orchestrator();
    
    if (!advance_to_next_root()) {
      _at_end = true;
    }
  }

  Entry &get_entry() {
    if (!_current_entry) {
      throw std::logic_error("Cannot dereference end iterator");
    }
    return *_current_entry;
  }

  void advance() {
    if (_at_end) {
      return;
    }

    bool request_descend_into_archive = _current_entry && _current_entry->descent_enabled() && !_current_entry->is_directory();

    if (_current_entry->depth() == 0 && request_descend_into_archive && !_current_entry->is_directory()) {
      request_descend_into_archive = false;
      attempt_descend_into_root(_current_entry->path_hierarchy());
    } 
    _current_entry.reset();

    if (fetch_from_archive(request_descend_into_archive)) {
      return;
    }

    if (fetch_from_directory()) {
      return;
    }

    if (advance_to_next_root()) {
      return;
    }
    
    descend_pending_multi_volumes();
    
    if (fetch_from_archive(false)) {
      return;
    }
    
    _at_end = true;
  }

  bool equals(const Impl *other) const {
    if (this == other) {
      return true;
    }
    if (!other) {
      return false;
    }
    return _at_end && other->_at_end;
  }

private:
  std::shared_ptr<ArchiveStackOrchestrator> ensure_shared_orchestrator() {
    if (!_shared_orchestrator) {
      _shared_orchestrator = std::make_shared<ArchiveStackOrchestrator>(_archive_options);
    }
    return _shared_orchestrator;
  }

  std::string normalize_path_string(const std::string &value) {
    if (value.empty()) {
      return value;
    }
    std::filesystem::path path_value(value);
    return path_value.lexically_normal().string();
  }

  bool fetch_from_directory() {
    if (_directory_iterator == _directory_end) {
      return false;
    }
    const std::filesystem::directory_entry entry = *_directory_iterator;
    set_current_entry(make_single_path(normalize_path_string(entry.path().string())));
    if (_current_entry->is_directory() && !_current_entry->descent_enabled()) {
      _directory_iterator.disable_recursion_pending();
    }
    ++_directory_iterator;
    return true;
  }

  bool fetch_from_archive(bool request_descend_into_archive) {
    if (!archive_active()) {
      return false;
    }
    ArchiveStackOrchestrator &orchestrator = *ensure_shared_orchestrator();

    try {
      if (orchestrator.advance(request_descend_into_archive)) {
        set_current_entry(orchestrator.current_entry_hierarchy());
        return true;
      }
    } catch (const EntryFaultError &error) {
      EntryFault fault = enrich_orchestrator_error(error, orchestrator);
      handle_orchestrator_error(fault);
    }
    return false;
  }

  bool advance_to_next_root() {
    if (_current_path_index >= _paths.size()) {
      return false;
    }
    const PathHierarchy &hierarchy = _paths[_current_path_index];
    reset_source_state();
    set_current_entry(hierarchy);
    if (hierarchy.size() == 1 && hierarchy.front().is_single()) {
      const std::filesystem::path fs_path(hierarchy.front().single_value());
      std::error_code ec;
      const bool path_is_directory = std::filesystem::is_directory(fs_path, ec) && !ec;
      if (path_is_directory) {
        _directory_iterator = std::filesystem::recursive_directory_iterator(fs_path, std::filesystem::directory_options::skip_permission_denied);
        _directory_end = std::filesystem::recursive_directory_iterator();
      }
    }
    ++_current_path_index;
    return true;
  }

  bool descend_pending_multi_volumes() {
    auto orchestrator = ensure_shared_orchestrator();
    try {
      if (orchestrator->descend_pending_multi_volumes()) {
        return true;
      }
    } catch (const EntryFaultError &error) {
      EntryFault fault = enrich_orchestrator_error(error, *orchestrator);
      handle_orchestrator_error(fault);
    }
    return false;
  }

  void attempt_descend_into_root(const PathHierarchy &hierarchy) {
    auto shared_orchestrator = ensure_shared_orchestrator();
    try {
      shared_orchestrator->open_root_hierarchy(hierarchy);
    } catch (const EntryFaultError &error) {
      EntryFault fault = enrich_orchestrator_error(error, *shared_orchestrator);
      handle_orchestrator_error(fault);
    }
  }

  void set_current_entry(PathHierarchy hierarchy) {
    _current_entry = Entry::create(std::move(hierarchy), ensure_shared_orchestrator(), _default_descent);
  }

  void handle_orchestrator_error(const EntryFault &fault) {
    dispatch_registered_fault(fault);
  }

  void reset_source_state() {
    reset_directory_traversal();
    _current_entry.reset();
  }

  bool archive_active() const { return _shared_orchestrator && _shared_orchestrator->depth() > 0; }

  bool directory_traversal_active() const { return _directory_iterator != _directory_end; }

  void reset_directory_traversal() {
    _directory_iterator = std::filesystem::recursive_directory_iterator();
    _directory_end = std::filesystem::recursive_directory_iterator();
  }

  EntryFault enrich_orchestrator_error(const EntryFaultError &error, ArchiveStackOrchestrator &orchestrator) {
    EntryFault fault = error.fault();
    if (fault.hierarchy.empty()) {
      fault.hierarchy = orchestrator.current_entry_hierarchy();
    }
    if (fault.message.empty() && error.what()) {
      fault.message = error.what();
    }
    return fault;
  }

  std::vector<PathHierarchy> _paths;
  size_t _current_path_index = 0;
  std::filesystem::recursive_directory_iterator _directory_iterator;
  std::filesystem::recursive_directory_iterator _directory_end;
  bool _at_end = false;
  std::unique_ptr<Entry> _current_entry;

  ArchiveOption _archive_options;
  std::shared_ptr<ArchiveStackOrchestrator> _shared_orchestrator;
  bool _default_descent = true;
};
// ============================================================================
// Iterator public interface
// ============================================================================

Traverser::Iterator::Iterator(std::unique_ptr<Impl> impl)
  : _impl(std::move(impl)) {}

Traverser::Iterator::~Iterator() = default;

Traverser::Iterator::Iterator(Iterator &&other) noexcept
    : _impl(std::move(other._impl)) {}

Traverser::Iterator &Traverser::Iterator::operator=(Iterator &&other) noexcept {
  _impl = std::move(other._impl);
  return *this;
}

Traverser::Iterator::reference Traverser::Iterator::operator*() { return _impl->get_entry(); }

Traverser::Iterator::pointer Traverser::Iterator::operator->() { return &_impl->get_entry(); }

Traverser::Iterator &Traverser::Iterator::operator++() {
  if (_impl) {
    _impl->advance();
  }
  return *this;
}

bool Traverser::Iterator::operator==(const Iterator &other) const {
  if (!_impl && !other._impl) {
    return true;
  }
  if (!_impl || !other._impl) {
    return false;
  }
  return _impl->equals(other._impl.get());
}

bool Traverser::Iterator::operator!=(const Iterator &other) const { return !(*this == other); }

// ============================================================================
// Traverser public interface
// ============================================================================

Traverser::Iterator Traverser::begin() { return Iterator(std::make_unique<Iterator::Impl>(_initial_paths, false, _options)); }

Traverser::Iterator Traverser::end() { return Iterator(std::make_unique<Iterator::Impl>(_initial_paths, true, _options)); }

} // namespace archive_r
