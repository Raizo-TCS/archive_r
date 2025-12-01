// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "archive_r/multi_volume_stream_base.h"
#include "archive_r/platform_compat.h"
#include "archive_r/entry_metadata.h"
#include "archive_r/path_hierarchy.h"

namespace archive_r {

class SystemFileStream : public MultiVolumeStreamBase {
public:
  explicit SystemFileStream(PathHierarchy logical_path);
  ~SystemFileStream() override;

private:
  void open_single_part(const PathHierarchy &single_part) override;
  void close_single_part() override;
  ssize_t read_from_single_part(void *buffer, size_t size) override;
  int64_t seek_within_single_part(int64_t offset, int whence) override;
  int64_t size_of_single_part(const PathHierarchy &single_part) override;

  void report_read_failure(int err);

  FILE *_handle;
  std::string _active_path;
};

struct FilesystemMetadataInfo {
  uint64_t size = 0;
  mode_t filetype = 0;
  EntryMetadataMap metadata;
};

FilesystemMetadataInfo collect_root_path_metadata(const PathHierarchy &hierarchy, const std::unordered_set<std::string> &allowed_keys);

} // namespace archive_r
