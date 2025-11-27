// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace archive_r {

/** POSIX-style timestamp with sub-second precision. */
struct EntryMetadataTime {
  int64_t seconds;
  int32_t nanoseconds;
};

/** Sparse file chunk (offset + stored length). */
struct EntryMetadataSparseChunk {
  int64_t offset;
  int64_t length;
};

/** Extended attribute key/value pair. */
struct EntryMetadataXattr {
  std::string name;
  std::vector<uint8_t> value;
};

/** Generic digest (algorithm + raw bytes). */
struct EntryMetadataDigest {
  std::string algorithm;
  std::vector<uint8_t> value;
};

/** Device identifiers for special files. */
struct EntryMetadataDeviceNumbers {
  uint64_t major;
  uint64_t minor;
};

/** BSD-style file flags (bits to set/clear). */
struct EntryMetadataFileFlags {
  uint64_t set;
  uint64_t clear;
};

using EntryMetadataValue = std::variant<std::monostate, bool, int64_t, uint64_t, std::string, std::vector<uint8_t>, EntryMetadataTime, EntryMetadataDeviceNumbers, EntryMetadataFileFlags,
                                        std::vector<EntryMetadataXattr>, std::vector<EntryMetadataSparseChunk>, std::vector<EntryMetadataDigest>>;

/** Unordered map storing metadata captured during traversal. */
using EntryMetadataMap = std::unordered_map<std::string, EntryMetadataValue>;

} // namespace archive_r
