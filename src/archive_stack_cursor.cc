// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_stack_cursor.h"

#include "archive_r/path_hierarchy_utils.h"
#include "system_file_stream.h"
#include <exception>
#include <memory>
#include <stdexcept>
#include <typeinfo>
#include <utility>

namespace archive_r {

namespace {

[[noreturn]] void throw_entry_fault(const std::string &message, const PathHierarchy &hierarchy) { throw make_entry_fault_error(message, hierarchy); }

} // namespace

// ============================================================================
// StreamArchive Implementation
// ============================================================================

StreamArchive::StreamArchive(std::shared_ptr<IDataStream> stream, ArchiveOption options)
    : Archive()
    , _stream(std::move(stream))
    , _options(std::move(options)) {
  if (!_stream) {
    throw std::invalid_argument("StreamArchive requires a valid data stream");
  }

  open_archive();
}

StreamArchive::~StreamArchive() = default;

void StreamArchive::open_archive() {
  archive_ptr ar = new_read_archive_common(_options.passphrases, _options.formats, [this](struct archive *ar) -> int {
    archive_read_set_callback_data(ar, this);
    archive_read_set_read_callback(ar, read_callback_bridge);
    if (_stream->can_seek()) {
      archive_read_set_skip_callback(ar, skip_callback_bridge);
      archive_read_set_seek_callback(ar, seek_callback_bridge);
    }
    return archive_read_open1(ar);
  });

  _ar = ar.release();
  current_entryname.clear();
  _at_eof = false;
  _current_entry_content_ready = false;
}

void StreamArchive::rewind() {
  _stream->rewind();
  Archive::rewind();
}

PathHierarchy StreamArchive::source_hierarchy() const { return _stream->source_hierarchy(); }

std::shared_ptr<StreamArchive> StreamArchive::parent_archive() const {
  auto entry_stream = std::dynamic_pointer_cast<EntryPayloadStream>(_stream);
  return entry_stream ? entry_stream->parent_archive() : nullptr;
}

la_ssize_t StreamArchive::read_callback_bridge(struct archive *a, void *client_data, const void **buff) {
  auto *archive = static_cast<StreamArchive *>(client_data);

  ssize_t bytes_read = 0;
  try {
    bytes_read = archive->_stream->read(archive->_buffer.data(), archive->_buffer.size());
  } catch (const std::exception &) {
    return -1;
  }
  if (bytes_read < 0) {
    return static_cast<la_ssize_t>(bytes_read);
  }

  *buff = archive->_buffer.data();

  return static_cast<la_ssize_t>(bytes_read);
}

la_int64_t StreamArchive::seek_callback_bridge(struct archive *a, void *client_data, la_int64_t request, int whence) {
  auto *archive = static_cast<StreamArchive *>(client_data);
  try {
    return archive->_stream->seek(request, whence);
  } catch (const std::exception &) {
    return -1;
  }
}

la_int64_t StreamArchive::skip_callback_bridge(struct archive *a, void *client_data, la_int64_t request) {
  auto *archive = static_cast<StreamArchive *>(client_data);
  try {
    la_int64_t current = archive->_stream->tell();
    if (current < 0) {
      current = archive->_stream->seek(0, SEEK_CUR);
    }
    if (current < 0) {
      return 0;
    }

    auto result = archive->_stream->seek(request, SEEK_CUR);
    if (result >= 0) {
      return result - current;
    }
  } catch (const std::exception &) {
    return 0;
  }
  return 0;
}

// ============================================================================
// EntryPayloadStream Implementation
// ============================================================================

EntryPayloadStream::EntryPayloadStream(std::shared_ptr<StreamArchive> parent_archive, PathHierarchy logical_path)
    : MultiVolumeStreamBase(std::move(logical_path), false)
    , _parent_archive(std::move(parent_archive)) {
  if (!_parent_archive) {
    throw std::invalid_argument("Invalid parent archive context");
  }
}

EntryPayloadStream::~EntryPayloadStream() {
  deactivate_active_part();
}

std::shared_ptr<StreamArchive> EntryPayloadStream::parent_archive() const { return _parent_archive; }

void EntryPayloadStream::rewind() {
  MultiVolumeStreamBase::rewind();
  const PathHierarchy first_part = pathhierarchy_select_single_part(_logical_path, 0);
  const std::string entry_name = first_part.back().single_value();

  if (!_parent_archive->skip_to_entry(entry_name)) {
    throw_entry_fault("Parent archive does not contain requested stream part", first_part);
  }
  // leave parent positioned at the beginning of the first part so subsequent reads start cleanly
}

void EntryPayloadStream::open_single_part(const PathHierarchy &single_part) {
  const std::string entry_name = single_part.back().single_value();
  if (!_parent_archive->skip_to_entry(entry_name)) {
    throw_entry_fault("Parent archive does not contain requested stream part", single_part);
  }
}

void EntryPayloadStream::close_single_part() {
  // libarchive automatically skips unread data when reading the next header,
  // so explicit skipping here is unnecessary and avoids potential exceptions in destructor.
}

ssize_t EntryPayloadStream::read_from_single_part(void *buffer, size_t size) {
  if (size == 0) {
    return 0;
  }
  return _parent_archive->read_current(buffer, size);
}

int64_t EntryPayloadStream::seek_within_single_part(int64_t offset, int whence) { return -1; }

int64_t EntryPayloadStream::size_of_single_part(const PathHierarchy &single_part) {
  (void)single_part;
  return -1;
}

// ============================================================================
// ArchiveStackCursor Implementation
// ============================================================================

ArchiveStackCursor::ArchiveStackCursor()
    : options_snapshot()
    , _current_stream(nullptr)
    , _current_archive(nullptr) {}

void ArchiveStackCursor::configure(const ArchiveOption &options) {
  options_snapshot = options;
}

void ArchiveStackCursor::reset() {
  options_snapshot = ArchiveOption{};
  _current_stream = nullptr;
  _current_archive = nullptr;
}

bool ArchiveStackCursor::descend() {
  if (!_current_stream) {
    throw std::logic_error("current stream is empty");
  }

  auto stream = _current_stream;
  if (auto *archive = current_archive()) {
    if (stream && !archive->current_entry_content_ready()) {
      stream->rewind();
    }
  }

  PathHierarchy dummy_hierarchy = stream->source_hierarchy();
  auto archive_ptr = std::make_shared<StreamArchive>(std::move(stream), options_snapshot);
  _current_archive = archive_ptr;
  _current_stream = nullptr;
  return true;
}

bool ArchiveStackCursor::ascend() {
  if (depth() <= 0) {
    return false;
  }

  if (_current_archive) {
    _current_stream = _current_archive->get_stream();
    _current_archive = _current_archive->parent_archive();
  } else {
    _current_stream = nullptr;
  }

  return true;
}

bool ArchiveStackCursor::next() {
  StreamArchive *archive = current_archive();
  if (!archive) {
    return false;
  }

  _current_stream = nullptr;

  while (true) {
    if (!archive->skip_to_next_header()) {
      return false;
    }
    if (!archive->current_entryname.empty()) {
      break;
    }
  }

  _current_stream = create_stream(current_entry_hierarchy());
  return true;
}

bool ArchiveStackCursor::synchronize_to_hierarchy(const PathHierarchy &target_hierarchy) {
  if (target_hierarchy.empty()) {
    throw_entry_fault("target hierarchy cannot be empty", {});
  }

  // 1. Ascend until we find a common ancestor
  while (depth() > 0) {
    auto current_h = _current_archive->source_hierarchy();
    if (current_h.size() <= target_hierarchy.size() &&
        hierarchies_equal(current_h, pathhierarchy_prefix_until(target_hierarchy, current_h.size() - 1))) {
      break;
    }
    ascend();
  }

  // 2. Descend to target
  for (size_t d = depth(); d < target_hierarchy.size(); ++d) {
    auto prefix = pathhierarchy_prefix_until(target_hierarchy, d);

    if (!_current_stream || !hierarchies_equal(_current_stream->source_hierarchy(), prefix)) {
      _current_stream = create_stream(prefix);
      _current_stream->rewind();
    }

    if (d < target_hierarchy.size() - 1) {
      descend();
    }
  }

  return true;
}

ssize_t ArchiveStackCursor::read(void *buff, size_t len) {
  if (len == 0) {
    return 0;
  }

  if (StreamArchive *archive = current_archive()) {
    return archive->read_current(buff, len);
  }

  if (_current_stream) {
    return _current_stream->read(buff, len);
  }
  return 0;
}

StreamArchive *ArchiveStackCursor::current_archive() {
  return _current_archive.get();
}

PathHierarchy ArchiveStackCursor::current_entry_hierarchy() {
  if (depth() == 0 || (!_current_stream && !_current_archive)) {
    return {};
  }

  if (StreamArchive *archive = current_archive()) {
    PathHierarchy path = archive->source_hierarchy();
    if (!archive->current_entryname.empty()) {
      append_single(path, archive->current_entryname);
    }
    return path;
  }

  return _current_stream->source_hierarchy();
}

std::shared_ptr<IDataStream> ArchiveStackCursor::create_stream(const PathHierarchy &hierarchy) {
  if (hierarchy.size() == 1) {
    if (auto factory = get_root_stream_factory()) {
      if (auto stream = factory(hierarchy)) {
        return stream;
      }
    }
    return std::make_shared<SystemFileStream>(hierarchy);
  }
  return std::make_shared<EntryPayloadStream>(_current_archive, hierarchy);
}

} // namespace archive_r
