// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include "archive_r/entry.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#ifdef max
#undef max
#endif

#ifdef min
#undef min
#endif

namespace archive_r::test_helpers {

inline std::vector<uint8_t> read_entry_fully(Entry &entry, size_t chunk_size = 64 * 1024) {
  if (chunk_size == 0) {
    chunk_size = 1;
  }

  std::vector<uint8_t> result;
  const uint64_t reported_size = entry.size();
  if (reported_size > 0 && reported_size <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
    result.reserve(static_cast<size_t>(reported_size));
  }

  std::vector<uint8_t> buffer(chunk_size);
  while (true) {
    const ssize_t bytes_read = entry.read(buffer.data(), buffer.size());
    if (bytes_read < 0) {
      throw std::runtime_error("failed to read entry payload");
    }
    if (bytes_read == 0) {
      break;
    }
    result.insert(result.end(), buffer.begin(), buffer.begin() + bytes_read);
  }

  return result;
}

} // namespace archive_r::test_helpers
