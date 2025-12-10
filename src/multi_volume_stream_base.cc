// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/multi_volume_stream_base.h"

#include "archive_r/path_hierarchy_utils.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <vector>

namespace archive_r {

struct MultiVolumeStreamBase::Impl {
  explicit Impl(MultiVolumeStreamBase &owner)
      : self(owner) {}

  MultiVolumeStreamBase &self;
  std::vector<int64_t> part_offsets;
  std::size_t total_parts = 0;
  std::size_t active_part_index = 0;
  std::size_t open_part_index = 0;
  bool part_open = false;
  int64_t logical_offset = 0;
  int64_t total_size = -1;

  void ensure_part_active(std::size_t part_index);
  bool advance_to_next_part();
  bool ensure_size_metadata();
  int64_t compute_target_offset(int64_t offset, int whence) const;
  std::size_t locate_part_for_offset(int64_t target, int64_t &offset_within_part) const;
};

MultiVolumeStreamBase::MultiVolumeStreamBase(PathHierarchy logical_path, bool supports_seek)
  : _logical_path(std::move(logical_path))
  , _supports_seek(supports_seek)
  , _impl(std::make_unique<Impl>(*this)) {
  _impl->total_parts = pathhierarchy_volume_size(_logical_path);
  if (_impl->total_parts == 0) {
    throw std::invalid_argument("MultiVolumeStreamBase requires at least one volume component");
  }
  _impl->part_offsets.assign(_impl->total_parts + 1, 0);
}

MultiVolumeStreamBase::~MultiVolumeStreamBase() = default;

ssize_t MultiVolumeStreamBase::read(void *buffer, size_t size) {
  if (size == 0) {
    return 0;
  }

  std::size_t total_read = 0;
  auto *out = static_cast<char *>(buffer);

  while (total_read < size) {
    if (_impl->active_part_index >= _impl->total_parts) {
      break;
    }

    _impl->ensure_part_active(_impl->active_part_index);
    const ssize_t bytes = read_from_single_part(out + total_read, size - total_read);
    if (bytes > 0) {
      total_read += static_cast<std::size_t>(bytes);
      _impl->logical_offset += bytes;
      continue;
    }

    if (bytes < 0) {
      return bytes;
    }

    if (!_impl->advance_to_next_part()) {
      break;
    }
  }

  return static_cast<ssize_t>(total_read);
}

void MultiVolumeStreamBase::rewind() {
  deactivate_active_part();
  _impl->active_part_index = 0;
  _impl->logical_offset = 0;
}

bool MultiVolumeStreamBase::at_end() const {
  return _impl->active_part_index >= _impl->total_parts;
}

int64_t MultiVolumeStreamBase::seek(int64_t offset, int whence) {
  if (!_supports_seek) {
    return -1;
  }
  if (!_impl->ensure_size_metadata()) {
    return -1;
  }

  const int64_t target = _impl->compute_target_offset(offset, whence);
  if (target < 0 || target > _impl->total_size) {
    return -1;
  }

  if (target == _impl->total_size) {
    deactivate_active_part();
    _impl->active_part_index = _impl->total_parts;
    _impl->logical_offset = target;
    return _impl->logical_offset;
  }

  int64_t offset_within_part = 0;
  const std::size_t part_index = _impl->locate_part_for_offset(target, offset_within_part);
  _impl->ensure_part_active(part_index);
  if (seek_within_single_part(offset_within_part, SEEK_SET) < 0) {
    return -1;
  }

  _impl->logical_offset = target;
  _impl->active_part_index = part_index;
  return _impl->logical_offset;
}

int64_t MultiVolumeStreamBase::tell() const { return _impl->logical_offset; }

void MultiVolumeStreamBase::Impl::ensure_part_active(std::size_t part_index) {
  if (part_open && open_part_index == part_index) {
    return;
  }

  self.deactivate_active_part();
  PathHierarchy single_part = pathhierarchy_select_single_part(self._logical_path, part_index);
  self.open_single_part(single_part);
  open_part_index = part_index;
  part_open = true;
}

void MultiVolumeStreamBase::deactivate_active_part() {
  if (_impl->part_open) {
    close_single_part();
    _impl->part_open = false;
  }
}

bool MultiVolumeStreamBase::Impl::advance_to_next_part() {
  if (active_part_index >= total_parts) {
    return false;
  }
  ++active_part_index;
  return active_part_index < total_parts;
}

bool MultiVolumeStreamBase::Impl::ensure_size_metadata() {
  if (total_size >= 0) {
    return true;
  }

  int64_t prefix = 0;
  part_offsets[0] = 0;
  for (std::size_t index = 0; index < total_parts; ++index) {
    PathHierarchy single_part = pathhierarchy_select_single_part(self._logical_path, index);
    const int64_t size = self.size_of_single_part(single_part);
    if (size < 0) {
      total_size = -1;
      return false;
    }
    prefix += size;
    part_offsets[index + 1] = prefix;
  }

  total_size = prefix;
  return true;
}

int64_t MultiVolumeStreamBase::Impl::compute_target_offset(int64_t offset, int whence) const {
  switch (whence) {
    case SEEK_SET:
      return offset;
    case SEEK_CUR:
      return logical_offset + offset;
    case SEEK_END:
      return total_size + offset;
    default:
      return -1;
  }
}

std::size_t MultiVolumeStreamBase::Impl::locate_part_for_offset(int64_t target, int64_t &offset_within_part) const {
  auto it = std::upper_bound(part_offsets.begin(), part_offsets.end(), target);
  if (it == part_offsets.begin()) {
    offset_within_part = target;
    return 0;
  }

  const std::size_t index = static_cast<std::size_t>(std::distance(part_offsets.begin(), it) - 1);
  offset_within_part = target - part_offsets[index];
  return index;
}

} // namespace archive_r
