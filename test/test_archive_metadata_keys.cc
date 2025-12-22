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
    // `StreamArchive` opens the archive in its constructor, so exercise the "not initialized" paths
    // using a minimal dummy archive implementation.
    struct DummyArchive final : Archive {
      void open_archive() override {}
    } dummy;

    try {
      (void)dummy.skip_to_next_header();
      std::cerr << "Expected exception for skip_to_next_header on uninitialized Archive" << std::endl;
      return 1;
    } catch (const std::logic_error &) {
      // expected
    }

    auto stream = std::make_shared<SystemFileStream>(make_single_path(archive_path));
    StreamArchive archive(stream);
    archive.open_archive();

    if (!expect(archive.skip_to_next_header(), "skip_to_next_header failed")) {
      return 1;
    }

    // skip_to_entry short-circuit path (already at the requested entry and content ready).
    const std::string first_entry = archive.current_entryname;
    if (!expect(!first_entry.empty(), "first entry name should not be empty")) {
      return 1;
    }
    if (!expect(archive.skip_to_entry(first_entry), "skip_to_entry should succeed for current entry")) {
      return 1;
    }

    // Drive the archive to EOF to exercise EOF handling branches.
    if (!expect(archive.skip_to_eof(), "skip_to_eof failed")) {
      return 1;
    }
    if (!expect(archive._at_eof, "archive should be at EOF after skip_to_eof")) {
      return 1;
    }
    if (!expect(!archive.skip_to_next_header(), "skip_to_next_header should return false at EOF")) {
      return 1;
    }
    if (!expect(archive.current_entry == nullptr, "current_entry should be cleared at EOF")) {
      return 1;
    }
    if (!expect(archive.current_entryname.empty(), "current_entryname should be cleared at EOF")) {
      return 1;
    }

    // EOF -> rewind path inside skip_to_entry.
    if (!expect(archive.skip_to_entry(first_entry), "skip_to_entry should rewind and find entry after EOF")) {
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
