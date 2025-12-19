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

void dump_debug(struct archive_entry *e, const EntryMetadataMap &m) {
  std::cerr << "--- debug: libarchive entry state ---" << std::endl;
  if (!e) {
    std::cerr << "entry=null" << std::endl;
    return;
  }

  const char *pathname = archive_entry_pathname(e);
  const char *pathname_utf8 = archive_entry_pathname_utf8(e);
  std::cerr << "pathname=" << (pathname ? pathname : "(null)") << std::endl;
  std::cerr << "pathname_utf8=" << (pathname_utf8 ? pathname_utf8 : "(null)") << std::endl;

  const char *symlink = archive_entry_symlink(e);
  const char *symlink_utf8 = archive_entry_symlink_utf8(e);
  std::cerr << "symlink=" << (symlink ? symlink : "(null)") << std::endl;
  std::cerr << "symlink_utf8=" << (symlink_utf8 ? symlink_utf8 : "(null)") << std::endl;

  std::cerr << "size_is_set=" << (archive_entry_size_is_set(e) ? "true" : "false") << std::endl;
  std::cerr << "size=" << static_cast<long long>(archive_entry_size(e)) << std::endl;
  std::cerr << "sparse_count=" << archive_entry_sparse_count(e) << std::endl;
  archive_entry_sparse_reset(e);
  la_int64_t offset = 0;
  la_int64_t length = 0;
  int sparse_next_count = 0;
  while (archive_entry_sparse_next(e, &offset, &length) == ARCHIVE_OK) {
    ++sparse_next_count;
    std::cerr << "sparse_next[" << sparse_next_count << "] offset=" << static_cast<long long>(offset) << " length=" << static_cast<long long>(length) << std::endl;
  }

  std::cerr << "--- debug: returned metadata keys ---" << std::endl;
  for (const auto &kv : m) {
    std::cerr << "key=" << kv.first << std::endl;
  }
  std::cerr << "--- debug: end ---" << std::endl;
}

struct EntryHolder {
  struct archive_entry *entry = nullptr;
  explicit EntryHolder(struct archive_entry *e)
      : entry(e) {}
  ~EntryHolder() {
    if (entry) {
      archive_entry_free(entry);
    }
  }
  EntryHolder(const EntryHolder &) = delete;
  EntryHolder &operator=(const EntryHolder &) = delete;
};

struct DummyArchive final : Archive {
  void open_archive() override {}
};

void seed_common_fields(struct archive_entry *e) {
  archive_entry_set_filetype(e, AE_IFREG);
  archive_entry_set_perm(e, 0644);
  archive_entry_set_mode(e, 0100644);
  archive_entry_set_size(e, 4096);
  archive_entry_set_dev(e, 42);
  archive_entry_set_rdev(e, 7);
  archive_entry_set_ino(e, 99);
  archive_entry_set_ino64(e, 100);
  archive_entry_set_nlink(e, 2);

  archive_entry_set_atime(e, 1710000000, 123);
  archive_entry_set_birthtime(e, 1710000001, 456);
  archive_entry_set_ctime(e, 1710000002, 789);
  archive_entry_set_mtime(e, 1710000003, 321);

  archive_entry_set_fflags(e, 1UL, 2UL);
  (void)archive_entry_copy_fflags_text(e, "uappend,nodump");

  archive_entry_set_is_data_encrypted(e, 1);
  archive_entry_set_is_metadata_encrypted(e, 0);

  archive_entry_set_symlink_type(e, 1);

  // ACL
  (void)archive_entry_acl_add_entry(e, ARCHIVE_ENTRY_ACL_TYPE_ACCESS, ARCHIVE_ENTRY_ACL_READ | ARCHIVE_ENTRY_ACL_WRITE, ARCHIVE_ENTRY_ACL_USER, 1000, "user");

  // xattr
  const char xattr_value[] = { 'v', 'a', 'l' };
  archive_entry_xattr_add_entry(e, "user.key", xattr_value, sizeof(xattr_value));

  // sparse
  archive_entry_sparse_add_entry(e, 100, 200);

  // mac metadata
  const uint8_t mac_meta[] = { 0x01, 0x02, 0x03, 0x04 };
  archive_entry_copy_mac_metadata(e, mac_meta, sizeof(mac_meta));
}

bool check_expected_keys(const EntryMetadataMap &m) {
  const char *must_exist[] = {
    "pathname",
    "sourcepath",
    "symlink",
    "hardlink",
    "uname",
    "gname",
    "uid",
    "gid",
    "size",
    "dev",
    "rdev",
    "ino",
    "ino64",
    "nlink",
    "atime",
    "birthtime",
    "ctime",
    "mtime",
    "fflags",
    "fflags_text",
    "is_data_encrypted",
    "is_metadata_encrypted",
    "symlink_type",
    "acl_text",
    "acl_types",
    "xattr",
    "sparse",
    "mac_metadata",
  };

  for (const char *k : must_exist) {
    if (m.find(k) == m.end()) {
      std::cerr << "missing metadata key: " << k << std::endl;
      return false;
    }
  }
  return true;
}

} // namespace

int main() {
  try {
    DummyArchive a;
    const auto keys = all_metadata_keys();

    // 0) Ensure empty allowed_keys yields empty metadata.
    {
      EntryHolder h(archive_entry_new());
      a.current_entry = h.entry;
      const auto metadata = a.current_entry_metadata({});
      if (!expect(metadata.empty(), "empty allowed_keys should return empty metadata")) {
        return 1;
      }
      a.current_entry = nullptr;
    }

    // 1) UTF-8 setters populated: exercises the *_utf8 branches.
    {
      EntryHolder h(archive_entry_new());
      struct archive_entry *e = h.entry;

      archive_entry_set_pathname_utf8(e, "utf8/path.txt");
      archive_entry_copy_sourcepath(e, "/tmp/source");
      archive_entry_set_symlink_utf8(e, "utf8/symlink");
      archive_entry_set_hardlink_utf8(e, "utf8/hardlink");
      archive_entry_set_uname_utf8(e, "utf8_user");
      archive_entry_set_gname_utf8(e, "utf8_group");
      archive_entry_set_uid(e, 1000);
      archive_entry_set_gid(e, 2000);

      seed_common_fields(e);

      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(keys);
      if (!check_expected_keys(metadata)) {
        dump_debug(e, metadata);
        if (!expect(false, "expected keys missing (utf8 case)")) {
          return 1;
        }
        return 1;
      }
      a.current_entry = nullptr;
    }

    // 2) Plain setters only: exercises the non-utf8 fallback branches.
    {
      EntryHolder h(archive_entry_new());
      struct archive_entry *e = h.entry;

      archive_entry_set_pathname(e, "plain/path.txt");
      archive_entry_copy_sourcepath(e, "/tmp/source");
      archive_entry_set_symlink(e, "plain/symlink");
      archive_entry_set_hardlink(e, "plain/hardlink");
      archive_entry_set_uname(e, "plain_user");
      archive_entry_set_gname(e, "plain_group");
      archive_entry_set_uid(e, 1000);
      archive_entry_set_gid(e, 2000);

      seed_common_fields(e);

      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(keys);
      if (!check_expected_keys(metadata)) {
        dump_debug(e, metadata);
        if (!expect(false, "expected keys missing (plain case)")) {
          return 1;
        }
        return 1;
      }
      a.current_entry = nullptr;
    }

    // 3) UID/GID should be omitted when no name and id==0.
    {
      EntryHolder h(archive_entry_new());
      struct archive_entry *e = h.entry;

      archive_entry_set_pathname(e, "noid/path.txt");
      archive_entry_set_uid(e, 0);
      archive_entry_set_gid(e, 0);

      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(keys);
      if (!expect(metadata.find("uid") == metadata.end(), "uid should be absent when unset")) {
        return 1;
      }
      if (!expect(metadata.find("gid") == metadata.end(), "gid should be absent when unset")) {
        return 1;
      }
      a.current_entry = nullptr;
    }

  } catch (const std::exception &ex) {
    std::cerr << "Archive current_entry_metadata test failed: " << ex.what() << std::endl;
    return 1;
  }

  std::cout << "Archive current_entry_metadata exercised" << std::endl;
  return 0;
}
