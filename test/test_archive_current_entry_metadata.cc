// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_type.h"

#include <archive_entry.h>

#include <cstring>
#include <cstdint>
#include <iostream>
#include <sstream>
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

bool expect(bool condition, const std::string &message) {
  return expect(condition, message.c_str());
}

std::string cstr_debug(const char *s) {
  if (!s) {
    return "<null>";
  }
  std::ostringstream oss;
  oss << '"' << s << '"' << " (len=" << std::strlen(s) << ')';
  return oss.str();
}

std::string list_keys(const EntryMetadataMap &m) {
  std::ostringstream oss;
  oss << '[';
  bool first = true;
  for (const auto &kv : m) {
    if (!first) {
      oss << ", ";
    }
    first = false;
    oss << kv.first;
  }
  oss << ']';
  return oss.str();
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

};

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

std::string make_invalid_utf8(const char *prefix) {
  std::string s(prefix);
  s.push_back(static_cast<char>(0xFF));
  s += "suffix";
  return s;
}

void seed_common_fields(struct archive_entry *e) {
  archive_entry_set_pathname(e, "/test/path");
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
  // Also add an empty xattr value to exercise the (value && size > 0) false branch.
  archive_entry_xattr_add_entry(e, "user.empty", "", 0);
  // Also add a nullptr value to exercise the (value && ...) false branch explicitly.
  archive_entry_xattr_add_entry(e, "user.null", nullptr, 0);

  // sparse
  archive_entry_sparse_add_entry(e, 100, 200);

  // mac metadata
  const uint8_t mac_meta[] = { 0x01, 0x02, 0x03, 0x04 };
  archive_entry_copy_mac_metadata(e, mac_meta, sizeof(mac_meta));
}

bool verify_metadata_consistency(struct archive_entry *e, const std::unordered_set<std::string> &allowed_keys, const EntryMetadataMap &m) {
  auto wants = [&allowed_keys](const char *key) { return allowed_keys.find(key) != allowed_keys.end(); };

  // 0) Ensure the implementation never returns keys outside the allow-list.
  for (const auto &kv : m) {
    if (allowed_keys.find(kv.first) == allowed_keys.end()) {
      std::cerr << "returned key not in allow-list: " << kv.first << std::endl;
      return false;
    }
  }

  // 1) Core expectations (platform-independent): if the underlying libarchive getter says the
  //    value exists and the key is allowed, current_entry_metadata() must include it.

  // pathname (utf8 preferred, else fallback)
  if (wants("pathname")) {
    const char *pathname_utf8 = archive_entry_pathname_utf8(e);
    if (pathname_utf8 && *pathname_utf8) {
      if (m.find("pathname") == m.end()) {
        std::cerr << "missing metadata key: pathname" << std::endl;
        return false;
      }
    } else {
      const char *pathname = archive_entry_pathname(e);
      if (pathname && *pathname) {
        if (m.find("pathname") == m.end()) {
          std::cerr << "missing metadata key: pathname" << std::endl;
          return false;
        }
      }
    }
  }

  if (wants("sourcepath")) {
    const char *sourcepath = archive_entry_sourcepath(e);
    if (sourcepath && *sourcepath) {
      if (m.find("sourcepath") == m.end()) {
        std::cerr << "missing metadata key: sourcepath" << std::endl;
        return false;
      }
    }
  }

  if (wants("symlink")) {
    const char *symlink_utf8 = archive_entry_symlink_utf8(e);
    if (symlink_utf8 && *symlink_utf8) {
      if (m.find("symlink") == m.end()) {
        std::cerr << "missing metadata key: symlink" << std::endl;
        return false;
      }
    }
  }

  if (wants("hardlink")) {
    const char *hardlink_utf8 = archive_entry_hardlink_utf8(e);
    const char *hardlink = archive_entry_hardlink(e);
    if ((hardlink_utf8 && *hardlink_utf8) || (hardlink && *hardlink)) {
      if (m.find("hardlink") == m.end()) {
        std::cerr << "missing metadata key: hardlink" << std::endl;
        return false;
      }
    }
  }

  if (wants("uname")) {
    const char *uname_utf8 = archive_entry_uname_utf8(e);
    const char *uname = archive_entry_uname(e);
    if ((uname_utf8 && *uname_utf8) || (uname && *uname)) {
      if (m.find("uname") == m.end()) {
        std::cerr << "missing metadata key: uname" << std::endl;
        return false;
      }
    }
  }

  if (wants("gname")) {
    const char *gname_utf8 = archive_entry_gname_utf8(e);
    const char *gname = archive_entry_gname(e);
    if ((gname_utf8 && *gname_utf8) || (gname && *gname)) {
      if (m.find("gname") == m.end()) {
        std::cerr << "missing metadata key: gname" << std::endl;
        return false;
      }
    }
  }

  if (wants("uid")) {
    bool has_uid = archive_entry_uname(e) != nullptr;
    if (!has_uid) {
      has_uid = archive_entry_uid(e) != 0;
    }
    if (has_uid && m.find("uid") == m.end()) {
      std::cerr << "missing metadata key: uid" << std::endl;
      return false;
    }
  }

  if (wants("gid")) {
    bool has_gid = archive_entry_gname(e) != nullptr;
    if (!has_gid) {
      has_gid = archive_entry_gid(e) != 0;
    }
    if (has_gid && m.find("gid") == m.end()) {
      std::cerr << "missing metadata key: gid" << std::endl;
      return false;
    }
  }

  if (wants("perm") && m.find("perm") == m.end()) {
    std::cerr << "missing metadata key: perm" << std::endl;
    return false;
  }

  if (wants("mode") && m.find("mode") == m.end()) {
    std::cerr << "missing metadata key: mode" << std::endl;
    return false;
  }

  if (wants("filetype") && m.find("filetype") == m.end()) {
    std::cerr << "missing metadata key: filetype" << std::endl;
    return false;
  }

  if (wants("size") && archive_entry_size_is_set(e) && m.find("size") == m.end()) {
    std::cerr << "missing metadata key: size" << std::endl;
    return false;
  }

  if (wants("dev") && archive_entry_dev_is_set(e) && m.find("dev") == m.end()) {
    std::cerr << "missing metadata key: dev" << std::endl;
    return false;
  }

  if (wants("rdev")) {
    const dev_t rdev = archive_entry_rdev(e);
    if (rdev != 0 && m.find("rdev") == m.end()) {
      std::cerr << "missing metadata key: rdev" << std::endl;
      return false;
    }
  }

  if (archive_entry_ino_is_set(e)) {
    if (wants("ino") && m.find("ino") == m.end()) {
      std::cerr << "missing metadata key: ino" << std::endl;
      return false;
    }
    if (wants("ino64") && m.find("ino64") == m.end()) {
      std::cerr << "missing metadata key: ino64" << std::endl;
      return false;
    }
  }

  if (wants("nlink") && m.find("nlink") == m.end()) {
    std::cerr << "missing metadata key: nlink" << std::endl;
    return false;
  }

  if (wants("strmode")) {
    const char *strmode = archive_entry_strmode(e);
    if (strmode && *strmode && m.find("strmode") == m.end()) {
      std::cerr << "missing metadata key: strmode" << std::endl;
      return false;
    }
  }

  if (wants("atime") && archive_entry_atime_is_set(e) && m.find("atime") == m.end()) {
    std::cerr << "missing metadata key: atime" << std::endl;
    return false;
  }
  if (wants("birthtime") && archive_entry_birthtime_is_set(e) && m.find("birthtime") == m.end()) {
    std::cerr << "missing metadata key: birthtime" << std::endl;
    return false;
  }
  if (wants("ctime") && archive_entry_ctime_is_set(e) && m.find("ctime") == m.end()) {
    std::cerr << "missing metadata key: ctime" << std::endl;
    return false;
  }
  if (wants("mtime") && archive_entry_mtime_is_set(e) && m.find("mtime") == m.end()) {
    std::cerr << "missing metadata key: mtime" << std::endl;
    return false;
  }

  unsigned long fflags_set = 0;
  unsigned long fflags_clear = 0;
  archive_entry_fflags(e, &fflags_set, &fflags_clear);
  if (wants("fflags") && (fflags_set != 0 || fflags_clear != 0) && m.find("fflags") == m.end()) {
    std::cerr << "missing metadata key: fflags" << std::endl;
    return false;
  }

  if (wants("fflags_text")) {
    const char *fflags_text = archive_entry_fflags_text(e);
    if (fflags_text && *fflags_text && m.find("fflags_text") == m.end()) {
      std::cerr << "missing metadata key: fflags_text" << std::endl;
      return false;
    }
  }

  if (wants("is_data_encrypted")) {
    const int v = archive_entry_is_data_encrypted(e);
    if (v >= 0 && m.find("is_data_encrypted") == m.end()) {
      std::cerr << "missing metadata key: is_data_encrypted" << std::endl;
      return false;
    }
  }
  if (wants("is_metadata_encrypted")) {
    const int v = archive_entry_is_metadata_encrypted(e);
    if (v >= 0 && m.find("is_metadata_encrypted") == m.end()) {
      std::cerr << "missing metadata key: is_metadata_encrypted" << std::endl;
      return false;
    }
  }
  if (wants("is_encrypted")) {
    const int v = archive_entry_is_encrypted(e);
    if (v >= 0 && m.find("is_encrypted") == m.end()) {
      std::cerr << "missing metadata key: is_encrypted" << std::endl;
      return false;
    }
  }

  if (wants("symlink_type")) {
    const int symlink_type = archive_entry_symlink_type(e);
    if (symlink_type != 0 && m.find("symlink_type") == m.end()) {
      std::cerr << "missing metadata key: symlink_type" << std::endl;
      return false;
    }
  }

  return true;
}

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
      if (!verify_metadata_consistency(e, keys, metadata)) {
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
      if (!verify_metadata_consistency(e, keys, metadata)) {
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

    // 4) Subset allow-list: exercises the wants("...") == false branches.
    {
      const std::unordered_set<std::string> subset_keys = {
        "pathname",
        "uid",
        "perm",
        "size",
      };

      EntryHolder h(archive_entry_new());
      struct archive_entry *e = h.entry;

      archive_entry_set_pathname(e, "subset/path.txt");
      archive_entry_set_uid(e, 1234);
      archive_entry_set_perm(e, 0644);
      archive_entry_set_size(e, 42);

      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(subset_keys);

      if (!verify_metadata_consistency(e, subset_keys, metadata)) {
        if (!expect(false, "expected keys missing (subset allow-list case)")) {
          return 1;
        }
        return 1;
      }

      if (!expect(metadata.size() == subset_keys.size(), "subset allow-list should not return extra keys")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // 4c) xattr/sparse/mac metadata present but excluded from allow-list: exercises the
    //     (count>0 && wants==false) paths.
    {
      const std::unordered_set<std::string> exclude_blob_keys = {
        "pathname",
      };

      EntryHolder h(archive_entry_new());
      struct archive_entry *e = h.entry;
      archive_entry_set_pathname(e, "excluded_blobs/path.txt");
      seed_common_fields(e);

      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(exclude_blob_keys);
      if (!verify_metadata_consistency(e, exclude_blob_keys, metadata)) {
        if (!expect(false, "expected keys missing (excluded blobs case)")) {
          return 1;
        }
        return 1;
      }

      if (!expect(metadata.find("xattr") == metadata.end(), "xattr should be absent when not allowed")) {
        return 1;
      }
      if (!expect(metadata.find("sparse") == metadata.end(), "sparse should be absent when not allowed")) {
        return 1;
      }
      if (!expect(metadata.find("mac_metadata") == metadata.end(), "mac_metadata should be absent when not allowed")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // 4b) Optional metadata not set: exercises wants(key)==true but missing/unknown values.
    {
      const std::unordered_set<std::string> optional_keys = {
        "pathname",
        "atime",
        "size",
        "dev",
        "mtime",
        "birthtime",
        "ctime",
        "fflags",
        "xattr",
        "sparse",
        "mac_metadata",
        "is_data_encrypted",
        "is_metadata_encrypted",
        "is_encrypted",
      };

      EntryHolder h(archive_entry_new());
      struct archive_entry *e = h.entry;

      archive_entry_set_pathname(e, "unset_optional/path.txt");

      // Ensure the underlying libarchive reports these fields as "not set" so the tested
      // branches are exercised deterministically.
      if (!expect(archive_entry_size_is_set(e) == 0, "precondition failed: size should be unset")) {
        return 1;
      }
      if (!expect(archive_entry_dev_is_set(e) == 0, "precondition failed: dev should be unset")) {
        return 1;
      }

      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(optional_keys);

      if (!expect(metadata.find("pathname") != metadata.end(), "pathname should be present when set")) {
        return 1;
      }

      if (!expect(metadata.find("atime") == metadata.end(), "atime should be absent when not set")) {
        return 1;
      }
      if (!expect(metadata.find("size") == metadata.end(), "size should be absent when not set")) {
        return 1;
      }
      if (!expect(metadata.find("dev") == metadata.end(), "dev should be absent when not set")) {
        return 1;
      }
      if (!expect(metadata.find("mtime") == metadata.end(), "mtime should be absent when not set")) {
        return 1;
      }
      if (!expect(metadata.find("birthtime") == metadata.end(), "birthtime should be absent when not set")) {
        return 1;
      }
      if (!expect(metadata.find("ctime") == metadata.end(), "ctime should be absent when not set")) {
        return 1;
      }
      if (!expect(metadata.find("fflags") == metadata.end(), "fflags should be absent when not set")) {
        return 1;
      }
      if (!expect(metadata.find("xattr") == metadata.end(), "xattr should be absent when no xattrs exist")) {
        return 1;
      }
      if (!expect(metadata.find("sparse") == metadata.end(), "sparse should be absent when no sparse regions exist")) {
        return 1;
      }
      if (!expect(metadata.find("mac_metadata") == metadata.end(), "mac_metadata should be absent when not set")) {
        return 1;
      }
      if (!expect(metadata.find("is_data_encrypted") != metadata.end(), "is_data_encrypted should be present (false) when unset")) {
        return 1;
      }
      if (!expect(metadata.find("is_metadata_encrypted") != metadata.end(), "is_metadata_encrypted should be present (false) when unset")) {
        return 1;
      }
      if (!expect(metadata.find("is_encrypted") != metadata.end(), "is_encrypted should be present (false) when unset")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // 4d) fflags set-only / clear-only: exercises (fflags_set != 0 || fflags_clear != 0) sub-branches.
    {
      const std::unordered_set<std::string> fflags_only = {
        "pathname",
        "fflags",
      };

      // set-only
      {
        EntryHolder h(archive_entry_new());
        struct archive_entry *e = h.entry;
        archive_entry_set_pathname(e, "fflags/set_only.txt");
        archive_entry_set_fflags(e, 1UL, 0UL);

        a.current_entry = e;
        const auto metadata = a.current_entry_metadata(fflags_only);
        if (!expect(metadata.find("fflags") != metadata.end(), "fflags should be present when set-only")) {
          return 1;
        }
        a.current_entry = nullptr;
      }

      // clear-only
      {
        EntryHolder h(archive_entry_new());
        struct archive_entry *e = h.entry;
        archive_entry_set_pathname(e, "fflags/clear_only.txt");
        archive_entry_set_fflags(e, 0UL, 1UL);

        a.current_entry = e;
        const auto metadata = a.current_entry_metadata(fflags_only);
        if (!expect(metadata.find("fflags") != metadata.end(), "fflags should be present when clear-only")) {
          return 1;
        }
        a.current_entry = nullptr;
      }
    }

    // 5) "Deny" most keys while values exist: exercises compound conditions where wants("...") is false.
    {
      const std::unordered_set<std::string> perm_only = {
        "perm",
      };

      EntryHolder h(archive_entry_new());
      struct archive_entry *e = h.entry;

      // Populate many values, but only "perm" is allowed.
      archive_entry_set_pathname_utf8(e, "utf8/denied_path.txt");
      archive_entry_copy_sourcepath(e, "/tmp/denied_source");
      archive_entry_set_symlink_utf8(e, "utf8/denied_symlink");
      archive_entry_set_hardlink_utf8(e, "utf8/denied_hardlink");
      archive_entry_set_uname_utf8(e, "utf8_denied_user");
      archive_entry_set_gname_utf8(e, "utf8_denied_group");
      archive_entry_set_uid(e, 999);
      archive_entry_set_gid(e, 888);

      seed_common_fields(e);

      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(perm_only);

      if (!verify_metadata_consistency(e, perm_only, metadata)) {
        if (!expect(false, "expected keys missing (perm-only allow-list case)")) {
          return 1;
        }
        return 1;
      }

      if (!expect(metadata.size() == perm_only.size(), "perm-only allow-list should not return extra keys")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // 6) "pathname only": exercises wants("perm") == false and other numeric-key false branches.
    {
      const std::unordered_set<std::string> pathname_only = {
        "pathname",
      };

      EntryHolder h(archive_entry_new());
      struct archive_entry *e = h.entry;

      archive_entry_set_pathname(e, "plain/pathname_only.txt");
      seed_common_fields(e);

      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(pathname_only);

      if (!verify_metadata_consistency(e, pathname_only, metadata)) {
        if (!expect(false, "expected keys missing (pathname-only allow-list case)")) {
          return 1;
        }
        return 1;
      }

      if (!expect(metadata.size() == pathname_only.size(), "pathname-only allow-list should not return extra keys")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // 7) pathname_utf8 empty string: exercises the pathname utf8->fallback path.
    {
      const std::unordered_set<std::string> pathname_only = {
        "pathname",
      };

      EntryHolder h(archive_entry_new());
      struct archive_entry *e = h.entry;

      // Make pathname_utf8 present-but-empty so the implementation should fall back to pathname.
      archive_entry_set_pathname_utf8(e, "");
      archive_entry_set_pathname(e, "plain/fallback_pathname.txt");

      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(pathname_only);

      if (!verify_metadata_consistency(e, pathname_only, metadata)) {
        if (!expect(false, "expected keys missing (pathname utf8 empty fallback case)")) {
          return 1;
        }
        return 1;
      }

      if (!expect(metadata.size() == pathname_only.size(), "pathname-only allow-list should not return extra keys")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // 7c) pathname_utf8 preferred when present: exercises the (pathname_utf8 && *pathname_utf8 && wants("pathname")) true path.
    {
      const std::unordered_set<std::string> pathname_only = {"pathname"};

      EntryHolder h(archive_entry_new());
      struct archive_entry *e = h.entry;

      // Make sure both pathname and pathname_utf8 are populated with different values so we can
      // verify that pathname_utf8 is preferred.
      archive_entry_set_pathname(e, "plain/fallback_pathname.txt");
      archive_entry_set_pathname_utf8(e, "utf8/preferred_path.txt");

      const char *pathname_utf8 = archive_entry_pathname_utf8(e);
      if (!expect(pathname_utf8 != nullptr && *pathname_utf8 != '\0', "precondition failed: pathname_utf8 should be non-empty")) {
        return 1;
      }

      DummyArchive a;
      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(pathname_only);

      if (!expect(metadata.find("pathname") != metadata.end(), "pathname should be included from pathname_utf8")) {
        return 1;
      }

      const auto it = metadata.find("pathname");
      if (it == metadata.end()) {
        return 1;
      }
      const std::string *pathname = std::get_if<std::string>(&it->second);
      if (!expect(pathname != nullptr, "pathname should be a string")) {
        return 1;
      }
      if (!expect(*pathname == "utf8/preferred_path.txt", std::string("pathname should prefer pathname_utf8, got: ") + *pathname)) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // 7b) pathname_utf8 is null but pathname exists: exercises the fallback assignment branch.
    {
      const std::unordered_set<std::string> pathname_only = {"pathname"};
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;

      const std::string bad_path = make_invalid_utf8("bad_path_");
      archive_entry_set_pathname(e, bad_path.c_str());

      // On libarchive, invalid byte sequences should generally not produce a UTF-8 view.
      const char *pathname_utf8 = archive_entry_pathname_utf8(e);
      if (!expect(pathname_utf8 == nullptr || *pathname_utf8 == '\0',
                  std::string("expected pathname_utf8 to be null/empty for invalid bytes, got: ") + cstr_debug(pathname_utf8))) {
        return 1;
      }

      DummyArchive a;
      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(pathname_only);
      if (!expect(metadata.find("pathname") != metadata.end(), "pathname should be included via non-utf8 fallback")) {
        return 1;
      }
      a.current_entry = nullptr;
    }

    // Test case: current_entry is null (should return empty metadata regardless of allowed_keys).
    {
      DummyArchive a;
      const auto metadata = a.current_entry_metadata(all_metadata_keys());
      if (!expect(metadata.empty(), "current_entry null should return empty metadata")) {
        return 1;
      }
    }

    // Test case: allowed_keys is empty (should return empty metadata regardless of current_entry).
    {
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;
      seed_common_fields(e);

      DummyArchive a;
      a.current_entry = e;
      const auto metadata = a.current_entry_metadata({});
      if (!expect(metadata.empty(), "empty allowed_keys should return empty metadata")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // Test case: pathname is null (should not include pathname in metadata).
    {
      const std::unordered_set<std::string> pathname_only = {"pathname"};
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;
      seed_common_fields(e);
      // Do not set pathname or pathname_utf8.
      archive_entry_set_pathname(e, nullptr);

      DummyArchive a;
      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(pathname_only);

      if (!expect(metadata.find("pathname") == metadata.end(), "null pathname should not be included")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // Test case: uid/gid conditions (has_uid/gid false paths).
    {
      const std::unordered_set<std::string> uid_gid_only = {"uid", "gid"};
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;
      seed_common_fields(e);
      // Clear uname/gname and set uid/gid to 0 to make has_uid/has_gid false.
      archive_entry_set_uname(e, nullptr);
      archive_entry_set_gname(e, nullptr);
      archive_entry_set_uid(e, 0);
      archive_entry_set_gid(e, 0);

      DummyArchive a;
      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(uid_gid_only);

      if (!expect(metadata.find("uid") == metadata.end(), "uid should not be included when has_uid is false")) {
        return 1;
      }
      if (!expect(metadata.find("gid") == metadata.end(), "gid should not be included when has_gid is false")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // Test case: hardlink present.
    {
      const std::unordered_set<std::string> hardlink_only = {"hardlink"};
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;
      seed_common_fields(e);
      archive_entry_set_hardlink(e, "hardlink_target");

      DummyArchive a;
      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(hardlink_only);

      if (!expect(metadata.find("hardlink") != metadata.end(),
                 std::string("hardlink should be included") +
                   " | keys=" + list_keys(metadata) +
                   " | hardlink_utf8=" + cstr_debug(archive_entry_hardlink_utf8(e)) +
                   " | hardlink=" + cstr_debug(archive_entry_hardlink(e)))) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // Test case: hardlink fallback branch (hardlink_utf8 is null but hardlink exists).
    {
      const std::unordered_set<std::string> hardlink_only = {"hardlink"};
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;

      const std::string bad_hardlink = make_invalid_utf8("bad_hardlink_");
      archive_entry_set_hardlink(e, bad_hardlink.c_str());

      const char *hardlink_utf8 = archive_entry_hardlink_utf8(e);
      const char *hardlink = archive_entry_hardlink(e);
      if (!expect(hardlink != nullptr && *hardlink != '\0',
                  std::string("expected hardlink to be set, got: ") + cstr_debug(hardlink))) {
        return 1;
      }
      if (!expect(hardlink_utf8 == nullptr || *hardlink_utf8 == '\0',
                  std::string("expected hardlink_utf8 to be null/empty for invalid bytes, got: ") + cstr_debug(hardlink_utf8))) {
        return 1;
      }

      DummyArchive a;
      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(hardlink_only);
      if (!expect(metadata.find("hardlink") != metadata.end(), "hardlink should be included via non-utf8 fallback")) {
        return 1;
      }
      a.current_entry = nullptr;
    }

    // Test case: uname/gname fallback branches (uname_utf8/gname_utf8 are null but uname/gname exist).
    {
      const std::unordered_set<std::string> user_group_only = {"uname", "gname"};
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;

      const std::string bad_uname = make_invalid_utf8("bad_uname_");
      const std::string bad_gname = make_invalid_utf8("bad_gname_");
      archive_entry_set_uname(e, bad_uname.c_str());
      archive_entry_set_gname(e, bad_gname.c_str());

      const char *uname_utf8 = archive_entry_uname_utf8(e);
      const char *uname = archive_entry_uname(e);
      const char *gname_utf8 = archive_entry_gname_utf8(e);
      const char *gname = archive_entry_gname(e);

      if (!expect(uname != nullptr && *uname != '\0', std::string("expected uname to be set, got: ") + cstr_debug(uname))) {
        return 1;
      }
      if (!expect(gname != nullptr && *gname != '\0', std::string("expected gname to be set, got: ") + cstr_debug(gname))) {
        return 1;
      }
      if (!expect(uname_utf8 == nullptr || *uname_utf8 == '\0',
                  std::string("expected uname_utf8 to be null/empty for invalid bytes, got: ") + cstr_debug(uname_utf8))) {
        return 1;
      }
      if (!expect(gname_utf8 == nullptr || *gname_utf8 == '\0',
                  std::string("expected gname_utf8 to be null/empty for invalid bytes, got: ") + cstr_debug(gname_utf8))) {
        return 1;
      }

      DummyArchive a;
      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(user_group_only);
      if (!expect(metadata.find("uname") != metadata.end(), "uname should be included via non-utf8 fallback")) {
        return 1;
      }
      if (!expect(metadata.find("gname") != metadata.end(), "gname should be included via non-utf8 fallback")) {
        return 1;
      }
      a.current_entry = nullptr;
    }

    // Test case: symlink present.
    {
      const std::unordered_set<std::string> symlink_only = {"symlink"};
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;
      seed_common_fields(e);

      // IMPORTANT: Some libarchive builds do not expose symlink getters unless the entry
      // is marked as a symlink.
      archive_entry_set_filetype(e, AE_IFLNK);
      archive_entry_set_perm(e, 0777);
      archive_entry_set_mode(e, 0120777);
      archive_entry_set_symlink(e, "symlink_target");

      DummyArchive a;
      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(symlink_only);

      if (!expect(metadata.find("symlink") != metadata.end(),
                 std::string("symlink should be included") +
                   " | keys=" + list_keys(metadata) +
                   " | symlink_utf8=" + cstr_debug(archive_entry_symlink_utf8(e)) +
                   " | symlink=" + cstr_debug(archive_entry_symlink(e)))) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // Test case: xattr present (xattr_count > 0).
    {
      const std::unordered_set<std::string> xattr_only = {"xattr"};
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;
      seed_common_fields(e);
      // xattr is already present from archive_entry_new()

      DummyArchive a;
      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(xattr_only);

      if (!expect(metadata.find("xattr") != metadata.end(), "xattr should be included when xattr_count > 0")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // Test case: digests present.
    {
      const std::unordered_set<std::string> digests_only = {"digests"};
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;
      seed_common_fields(e);
      // digests are present

      DummyArchive a;
      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(digests_only);

      if (!expect(metadata.find("digests") != metadata.end(), "digests should be included when digests are present")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // Test case: null current_entry.
    {
      const std::unordered_set<std::string> some_keys = {"pathname"};
      DummyArchive a;
      a.current_entry = nullptr;
      const auto metadata = a.current_entry_metadata(some_keys);

      if (!expect(metadata.empty(), "metadata should be empty when current_entry is null")) {
        return 1;
      }
    }

    // Test case: specific keys requested.
    {
      const std::unordered_set<std::string> specific_keys = {"pathname", "size", "mode"};
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;
      seed_common_fields(e);

      DummyArchive a;
      a.current_entry = e;
      const auto metadata = a.current_entry_metadata(specific_keys);

      if (!expect(metadata.size() == 3, "metadata should have 3 entries")) {
        std::cerr << "actual size: " << metadata.size() << std::endl;
        return 1;
      }
      if (!expect(metadata.count("pathname"), "pathname should be present")) {
        return 1;
      }
      if (!expect(metadata.count("size"), "size should be present")) {
        return 1;
      }
      if (!expect(metadata.count("mode"), "mode should be present")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // Test case: current_entry_size
    {
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;
      seed_common_fields(e);

      DummyArchive a;
      a.current_entry = e;
      const uint64_t size = a.current_entry_size();

      if (!expect(size == 4096, "current_entry_size should return 4096")) {
        return 1;
      }

      a.current_entry = nullptr;
    }

    // Test case: current_entry_filetype
    {
      EntryHolder eh(archive_entry_new());
      struct archive_entry *e = eh.entry;
      seed_common_fields(e);

      DummyArchive a;
      a.current_entry = e;
      const mode_t filetype = a.current_entry_filetype();

      if (!expect(filetype == AE_IFREG, "current_entry_filetype should return AE_IFREG")) {
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
