// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include "archive_r/data_stream.h"
#include "archive_r/path_hierarchy.h"
#include "archive_type.h"
#include "entry_fault_error.h"
#include "archive_r/multi_volume_stream_base.h"
#include <array>
#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <vector>

namespace archive_r {

// ============================================================================
// StreamArchive Interface
// ============================================================================

class StreamArchive : public Archive {
public:
  explicit StreamArchive(std::shared_ptr<IDataStream> stream, ArchiveOption options = {});

  ~StreamArchive() override;

  void open_archive() override;
  void rewind() override;

  PathHierarchy source_hierarchy() const;
  std::shared_ptr<StreamArchive> parent_archive() const;

  std::shared_ptr<IDataStream> get_stream() const { return _stream; }

private:
  static la_ssize_t read_callback_bridge(struct archive *a, void *client_data, const void **buff);
  static la_int64_t seek_callback_bridge(struct archive *a, void *client_data, la_int64_t request, int whence);
  static la_int64_t skip_callback_bridge(struct archive *a, void *client_data, la_int64_t request);

  static constexpr size_t BUFFER_SIZE = 65536;
  std::shared_ptr<IDataStream> _stream;
  std::array<char, BUFFER_SIZE> _buffer;
  ArchiveOption _options;
};

// ============================================================================
// EntryPayloadStream Interface
// ============================================================================

class EntryPayloadStream : public MultiVolumeStreamBase {
public:
  EntryPayloadStream(std::shared_ptr<StreamArchive> parent_archive, PathHierarchy logical_path);
  ~EntryPayloadStream() override;

  std::shared_ptr<StreamArchive> parent_archive() const;
  void rewind() override;

private:
  std::shared_ptr<StreamArchive> _parent_archive;

  void open_single_part(const PathHierarchy &single_part) override;
  void close_single_part() override;
  ssize_t read_from_single_part(void *buffer, size_t size) override;
  int64_t seek_within_single_part(int64_t offset, int whence) override;
  int64_t size_of_single_part(const PathHierarchy &single_part) override;
};

// ============================================================================
// ArchiveStackCursor Interface
// ============================================================================

struct ArchiveStackCursor {

  ArchiveStackCursor();

  void configure(const ArchiveOption &options);
  void reset();
  bool has_stream() const { return _current_stream != nullptr; }

  bool descend();
  bool ascend();
  bool next();
  bool synchronize_to_hierarchy(const PathHierarchy &hierarchy);
  ssize_t read(void *buffer, size_t len);

  size_t depth() const {
    size_t d = 0;
    auto a = _current_archive;
    while (a) {
      d++;
      a = a->parent_archive();
    }
    return d;
  }

  StreamArchive *current_archive();

  PathHierarchy current_entry_hierarchy();

  std::shared_ptr<IDataStream> create_stream(const PathHierarchy &hierarchy);

  ArchiveOption options_snapshot;

private:
  std::shared_ptr<IDataStream> _current_stream;
  std::shared_ptr<StreamArchive> _current_archive;
};

} // namespace archive_r
