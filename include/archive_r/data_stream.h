// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include "archive_r/platform_compat.h"
#include "archive_r/path_hierarchy.h"

#include <functional>
#include <memory>
#include <cstdint>

namespace archive_r {

/**
 * @brief Abstract stream interface used by the archive traversal stack
 */
class IDataStream {
public:
  virtual ~IDataStream() = default;
  virtual ssize_t read(void *buffer, size_t size) = 0;
  virtual void rewind() = 0;
  virtual bool at_end() const = 0;
  virtual int64_t seek(int64_t offset, int whence) { return -1; }
  virtual int64_t tell() const { return -1; }
  virtual bool can_seek() const { return false; }
  virtual PathHierarchy source_hierarchy() const = 0;
};

using RootStreamFactory = std::function<std::shared_ptr<IDataStream>(const PathHierarchy &)>;

/**
 * @brief Register the default factory used for root PathHierarchy streams
 */
void set_root_stream_factory(RootStreamFactory factory);

/**
 * @brief Retrieve the currently registered root stream factory
 */
RootStreamFactory get_root_stream_factory();

} // namespace archive_r
