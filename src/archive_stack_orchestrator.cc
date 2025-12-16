// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_stack_orchestrator.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/entry_fault.h"
#include "system_file_stream.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <typeinfo>
#include <utility>

namespace archive_r {

ArchiveStackOrchestrator::ArchiveStackOrchestrator(const ArchiveOption &options)
    : _archive_options(options)
{
  _head.configure(_archive_options);
}

ArchiveStackOrchestrator::~ArchiveStackOrchestrator() = default;

size_t ArchiveStackOrchestrator::depth() const { return _head.depth(); }

StreamArchive *ArchiveStackOrchestrator::current_archive() { return _head.current_archive(); }

// Drives the traversal state machine:
// 1. Optionally descend into the current entry when requested.
// 2. Attempt to advance within the active archive; report faults but keep looping.
// 3. Drain any pending multi-volume groups before bubbling up to the parent so multipart
//    archives are consumed contiguously.
// 4. When leaving a multi-volume context, rewind the parent archive by skipping to EOF to
//    avoid re-reading already processed entries.
bool ArchiveStackOrchestrator::advance(bool descend_request) {
  bool request_descend = descend_request;

  while (true) {
    if (depth() == 0) {
      return false;
    }
    try {
      if (request_descend) {
        request_descend = false;
        _head.descend();
      }
    } catch (const EntryFaultError &error) {
      dispatch_fault(error.fault());
      continue;
    }

    try {
      if (_head.next()) {
        return true;
      }
    } catch (const EntryFaultError &error) {
      dispatch_fault(error.fault());
      continue;
    }

    try {
      // Consume any pending multi-volume siblings so we do not return to the parent mid-series.
      if (descend_pending_multi_volumes()) {
        continue;
      }
    } catch (const EntryFaultError &error) {
      dispatch_fault(error.fault());
    }

    PathHierarchy prev_ascend_hierarchy = _head.current_entry_hierarchy();
    _head.ascend();

    if (!pathhierarchy_is_multivolume(prev_ascend_hierarchy)) {
      continue;
    }

    try {
      // If same-level multi-volume siblings remain, keep draining them before touching the parent next().
      if (descend_pending_multi_volumes()) {
        continue;
      }
    } catch (const EntryFaultError &error) {
      dispatch_fault(error.fault());
    }
    try {
      StreamArchive *archive = _head.current_archive();
      if (archive) {
        // After all volumes are processed, push the parent back to EOF to avoid duplicate next().
        archive->skip_to_eof();
      }
    } catch (const EntryFaultError &error) {
      dispatch_fault(error.fault());
    }
  }
}

const std::string &ArchiveStackOrchestrator::current_entryname() {
  StreamArchive *archive = current_archive();
  if (!archive) {
    static const std::string empty;
    return empty;
  }
  return archive->current_entryname;
}

PathHierarchy ArchiveStackOrchestrator::current_entry_hierarchy() { return _head.current_entry_hierarchy(); }

bool ArchiveStackOrchestrator::synchronize_to_hierarchy(const PathHierarchy &path_hierarchy) {
  try {
    _head.synchronize_to_hierarchy(path_hierarchy);
    return true;
  } catch (const EntryFaultError &error) {
    dispatch_fault(error.fault());
    return false;
  }
}

ssize_t ArchiveStackOrchestrator::read_head(void *buff, size_t len) {
  try {
    return _head.read(buff, len);
  } catch (const EntryFaultError &error) {
    dispatch_fault(error.fault());
  }

  return -1;
}

void ArchiveStackOrchestrator::mark_entry_as_multi_volume(const PathHierarchy &entry_path, const std::string &base_name, PathEntry::Parts::Ordering ordering) {
  _multi_volume_manager.mark_entry_as_multi_volume(entry_path, base_name, ordering);
}

bool ArchiveStackOrchestrator::descend_pending_multi_volumes() {
  const PathHierarchy current_hierarchy = _head.current_entry_hierarchy();
  PathHierarchy multi_volume_target;
  if (!_multi_volume_manager.pop_multi_volume_group(current_hierarchy, multi_volume_target)) {
    return false;
  }
  
  _head.synchronize_to_hierarchy(multi_volume_target);
  _head.descend();
  return true;
}

void ArchiveStackOrchestrator::open_root_hierarchy(const PathHierarchy &root_hierarchy) {
  _head.synchronize_to_hierarchy(root_hierarchy);
  _head.descend();
}

void ArchiveStackOrchestrator::dispatch_fault(EntryFault fault) {
  if (fault.hierarchy.empty()) {
    fault.hierarchy = _head.current_entry_hierarchy();
  }

  dispatch_registered_fault(fault);
}

} // namespace archive_r
