// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_type.h"

#include <archive_entry.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

using namespace archive_r;

namespace {

struct DummyArchive final : Archive {
  void open_archive() override {}
};

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

void fill_entry_common_fields(struct archive_entry *e) {
  archive_entry_set_size(e, 123);
  archive_entry_set_uid(e, 1000);
  archive_entry_set_gid(e, 1001);
  archive_entry_set_perm(e, 0644);
  archive_entry_set_mode(e, 0100644);
  archive_entry_set_nlink(e, 2);

  // Ensure these “is_set” predicates can be true.
  archive_entry_set_ino64(e, 42);

  // Device numbers (use non-zero values)
  archive_entry_set_dev(e, 0x00010002);
  archive_entry_set_rdev(e, 0x00030004);

  // Times
  archive_entry_set_atime(e, 1700000000, 1);
  archive_entry_set_birthtime(e, 1700000001, 2);
  archive_entry_set_ctime(e, 1700000002, 3);
  archive_entry_set_mtime(e, 1700000003, 4);

  // File flags
  archive_entry_set_fflags(e, 1UL, 2UL);
}

EntryMetadataMap get_metadata(struct archive_entry *e, const std::unordered_set<std::string> &keys) {
  DummyArchive a;
  a.current_entry = e;
  return a.current_entry_metadata(keys);
}

void set_acl_simple(struct archive_entry *e) {
  // Basic user::rwx ACL entry to ensure acl_to_text() can return something.
  // (Permissions are not important for this test; we just need at least one ACL entry.)
  (void)archive_entry_acl_add_entry(e, ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_USER_OBJ, ARCHIVE_ENTRY_ACL_EXECUTE | ARCHIVE_ENTRY_ACL_READ | ARCHIVE_ENTRY_ACL_WRITE, -1, nullptr);
}

} // namespace

int main() {
  bool ok = true;

  // Case 1: pathname_utf8 path
  {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname_utf8(e, "utf8_path");
    archive_entry_copy_sourcepath(e, "/tmp/sourcepath");
    archive_entry_set_symlink_utf8(e, "symlink_utf8");
    archive_entry_set_hardlink_utf8(e, "hardlink_utf8");
    archive_entry_set_uname_utf8(e, "uname_utf8");
    archive_entry_set_gname_utf8(e, "gname_utf8");
    fill_entry_common_fields(e);

    // Add xattr/sparse where supported.
    archive_entry_xattr_add_entry(e, "user.test", "abc", 3);
    archive_entry_sparse_add_entry(e, 0, 10);

    // 1a) wants true for many keys
    const auto all_keys = all_metadata_keys();
    auto meta_all = get_metadata(e, all_keys);
    ok = expect(meta_all.find("pathname") != meta_all.end(), "Expected pathname in metadata (utf8)") && ok;

    // 1b) wants false branch for most keys (but function must run)
    const std::unordered_set<std::string> minimal_keys = { "pathname" };
    auto meta_min = get_metadata(e, minimal_keys);
    ok = expect(meta_min.find("pathname") != meta_min.end(), "Expected pathname in metadata (minimal)") && ok;
    ok = expect(meta_min.find("uid") == meta_min.end(), "Expected uid omitted when not requested") && ok;

    archive_entry_free(e);
  }

  // Case 2: fallback branches (non-utf8 variants)
  {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname(e, "non_utf8_path");
    archive_entry_set_hardlink(e, "hardlink_plain");
    archive_entry_set_uname(e, "uname_plain");
    archive_entry_set_gname(e, "gname_plain");
    fill_entry_common_fields(e);

    const auto all_keys = all_metadata_keys();
    auto meta_all = get_metadata(e, all_keys);
    ok = expect(meta_all.find("pathname") != meta_all.end(), "Expected pathname in metadata (fallback)") && ok;
    ok = expect(meta_all.find("hardlink") != meta_all.end(), "Expected hardlink present (fallback)") && ok;
    ok = expect(meta_all.find("uname") != meta_all.end(), "Expected uname present (fallback)") && ok;
    ok = expect(meta_all.find("gname") != meta_all.end(), "Expected gname present (fallback)") && ok;

    archive_entry_free(e);
  }

  // Case 3: explicit absences to cover initializer-false paths
  {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname_utf8(e, "no_optional_strings");
    // Do not set sourcepath/symlink/hardlink/uname/gname.

    const std::unordered_set<std::string> keys = { "pathname", "sourcepath", "symlink", "hardlink", "uname", "gname" };
    auto meta = get_metadata(e, keys);
    ok = expect(meta.find("pathname") != meta.end(), "Expected pathname present") && ok;
    ok = expect(meta.find("sourcepath") == meta.end(), "Expected sourcepath absent when unset") && ok;
    ok = expect(meta.find("symlink") == meta.end(), "Expected symlink absent when unset") && ok;
    ok = expect(meta.find("hardlink") == meta.end(), "Expected hardlink absent when unset") && ok;
    ok = expect(meta.find("uname") == meta.end(), "Expected uname absent when unset") && ok;
    ok = expect(meta.find("gname") == meta.end(), "Expected gname absent when unset") && ok;

    archive_entry_free(e);
  }

  // Case 4: uid/gid “has_*” false branches + time/is_set false branches
  {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname_utf8(e, "uid_gid_absent");

    // Keep uid/gid at 0 and omit uname/gname so has_uid/has_gid become false.
    archive_entry_set_uid(e, 0);
    archive_entry_set_gid(e, 0);
    // Do not set size/dev/ino/times/fflags.

    const std::unordered_set<std::string> keys = { "pathname", "uid", "gid", "size", "dev", "ino", "ino64", "atime", "birthtime", "ctime", "mtime", "fflags", "fflags_text" };
    auto meta = get_metadata(e, keys);
    ok = expect(meta.find("pathname") != meta.end(), "Expected pathname present") && ok;
    ok = expect(meta.find("uid") == meta.end(), "Expected uid omitted when not present") && ok;
    ok = expect(meta.find("gid") == meta.end(), "Expected gid omitted when not present") && ok;
    ok = expect(meta.find("size") == meta.end(), "Expected size omitted when not set") && ok;
    ok = expect(meta.find("dev") == meta.end(), "Expected dev omitted when not set") && ok;
    ok = expect(meta.find("ino") == meta.end(), "Expected ino omitted when not set") && ok;
    ok = expect(meta.find("ino64") == meta.end(), "Expected ino64 omitted when not set") && ok;
    ok = expect(meta.find("fflags") == meta.end(), "Expected fflags omitted when not set") && ok;

    archive_entry_free(e);
  }

  // Case 5: encryption flags (value <0 / ==0 / !=0)
  {
    const std::unordered_set<std::string> keys = { "pathname", "is_data_encrypted", "is_metadata_encrypted", "is_encrypted" };

    // 5a) default (expected to be <0 on some builds): should not store
    {
      struct archive_entry *e = archive_entry_new();
      archive_entry_set_pathname_utf8(e, "enc_default");
      auto meta = get_metadata(e, keys);
      (void)meta; // outcome is build-dependent; we just want to exercise the value<0 early-return branch if applicable.
      archive_entry_free(e);
    }

    // 5b) explicit false
    {
      struct archive_entry *e = archive_entry_new();
      archive_entry_set_pathname_utf8(e, "enc_false");
      archive_entry_set_is_data_encrypted(e, 0);
      archive_entry_set_is_metadata_encrypted(e, 0);
      auto meta = get_metadata(e, keys);
      ok = expect(meta.find("is_data_encrypted") != meta.end(), "Expected is_data_encrypted present when set") && ok;
      ok = expect(meta.find("is_metadata_encrypted") != meta.end(), "Expected is_metadata_encrypted present when set") && ok;
      archive_entry_free(e);
    }

    // 5c) explicit true
    {
      struct archive_entry *e = archive_entry_new();
      archive_entry_set_pathname_utf8(e, "enc_true");
      archive_entry_set_is_data_encrypted(e, 1);
      archive_entry_set_is_metadata_encrypted(e, 1);
      auto meta = get_metadata(e, keys);
      ok = expect(meta.find("is_data_encrypted") != meta.end(), "Expected is_data_encrypted present when set") && ok;
      ok = expect(meta.find("is_metadata_encrypted") != meta.end(), "Expected is_metadata_encrypted present when set") && ok;
      archive_entry_free(e);
    }
  }

  // Case 6: ACL + xattr/sparse counts with wants true/false
  {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname_utf8(e, "acl_xattr_sparse");
    set_acl_simple(e);
    archive_entry_xattr_add_entry(e, "user.test", "abc", 3);
    archive_entry_sparse_add_entry(e, 0, 10);

    const std::unordered_set<std::string> keys_all = { "pathname", "acl_text", "acl_types", "xattr", "sparse" };
    auto meta_all = get_metadata(e, keys_all);
    ok = expect(meta_all.find("acl_text") != meta_all.end(), "Expected acl_text present when acl exists") && ok;

    const std::unordered_set<std::string> keys_none = { "pathname" };
    auto meta_none = get_metadata(e, keys_none);
    ok = expect(meta_none.find("acl_text") == meta_none.end(), "Expected acl_text omitted when not requested") && ok;

    archive_entry_free(e);
  }

  // Case 7: wants true but xattr/sparse are empty (count==0 paths)
  {
    struct archive_entry *e = archive_entry_new();
    archive_entry_set_pathname_utf8(e, "no_xattr_sparse");
    const std::unordered_set<std::string> keys = { "pathname", "xattr", "sparse" };
    auto meta = get_metadata(e, keys);
    ok = expect(meta.find("xattr") == meta.end(), "Expected xattr omitted when none exist") && ok;
    ok = expect(meta.find("sparse") == meta.end(), "Expected sparse omitted when none exist") && ok;
    archive_entry_free(e);
  }

  if (!expect(ok, "archive_entry metadata branch tests failed")) {
    return 1;
  }

  std::cout << "Archive entry metadata branches exercised" << std::endl;
  return 0;
}
