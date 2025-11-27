// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include "archive_r/data_stream.h"
#include "archive_r/path_hierarchy.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace archive_r {

class MultiVolumeStreamBase : public IDataStream {
public:
  ~MultiVolumeStreamBase() override;

  ssize_t read(void *buffer, size_t size) override;
  void rewind() override;
  bool at_end() const override;
  int64_t seek(int64_t offset, int whence) override;
  int64_t tell() const override;
  bool can_seek() const override { return _supports_seek; }
  PathHierarchy source_hierarchy() const override { return _logical_path; }

protected:
  MultiVolumeStreamBase(PathHierarchy logical_path, bool supports_seek);

  virtual void open_single_part(const PathHierarchy &single_part) = 0;
  virtual void close_single_part() = 0;
  virtual ssize_t read_from_single_part(void *buffer, size_t size) = 0;
  virtual int64_t seek_within_single_part(int64_t offset, int whence) = 0;
  virtual int64_t size_of_single_part(const PathHierarchy &single_part) = 0;

  PathHierarchy _logical_path;
  void deactivate_active_part();

private:
  friend struct Impl;
  struct Impl;
  std::unique_ptr<Impl> _impl;
  const bool _supports_seek;
};

} // namespace archive_r
