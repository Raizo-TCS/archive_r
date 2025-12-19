// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_stack_cursor.h"
#include "system_file_stream.h"

#include <iostream>
#include <string>
#include <unordered_set>

using namespace archive_r;

namespace {

bool expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << std::endl;
    return false;
  }
  return true;
}

std::unordered_set<std::string> all_metadata_keys() {
  return {
    "pathname",
    "sourcepath",
    "symlink",
    "hardlink",
    "uname",
    "gname",
    "uid",
    "gid",
    "perm",
    "mode",
    "filetype",
    "size",
    "dev",
    "rdev",
    "ino",
    "ino64",
    "nlink",
    "strmode",
    "atime",
    "birthtime",
    "ctime",
    "mtime",
    "fflags",
    "fflags_text",
    "is_data_encrypted",
    "is_metadata_encrypted",
    "is_encrypted",
    "symlink_type",
    "acl_text",
    "acl_types",
    "xattr",
    "sparse",
    "mac_metadata",
    "digests",
  };
}

} // namespace

int main(int argc, char *argv[]) {
  const std::string archive_path = (argc >= 2) ? argv[1] : std::string("test_data/no_uid.zip");

  try {
    auto stream = std::make_shared<SystemFileStream>(make_single_path(archive_path));
    StreamArchive archive(stream);
    archive.open_archive();

    if (!expect(archive.skip_to_next_header(), "skip_to_next_header failed")) {
      return 1;
    }

    // Ensure empty allowed_keys yields empty metadata.
    const auto empty_metadata = archive.current_entry_metadata({});
    if (!expect(empty_metadata.empty(), "empty allowed_keys should return empty metadata")) {
      return 1;
    }

    // Exercise a wide set of metadata branches (even if many values are absent in the entry).
    const auto metadata = archive.current_entry_metadata(all_metadata_keys());
    if (!expect(metadata.find("pathname") != metadata.end(), "metadata should include pathname when requested")) {
      return 1;
    }

  } catch (const std::exception &ex) {
    std::cerr << "Archive metadata key coverage test failed: " << ex.what() << std::endl;
    return 1;
  }

  std::cout << "Archive metadata keys exercised" << std::endl;
  return 0;
}
