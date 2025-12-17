// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_type.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <unordered_map>

namespace archive_r {

namespace {

[[noreturn]] void throw_archive_fault(const std::string &message, struct archive *ar = nullptr) {
  const int err = ar ? archive_errno(ar) : 0;
  throw make_entry_fault_error(message, {}, err);
}

} // namespace

void archive_deleter::operator()(struct archive *a) const {
  if (a) {
    archive_read_close(a);
    archive_read_free(a);
  }
}

static void set_passphrases(struct archive *ar, const std::vector<std::string> &passphrases) {
  size_t index = 0;
  for (const auto &passphrase : passphrases) {
    if (archive_read_add_passphrase(ar, passphrase.c_str()) != ARCHIVE_OK) {
      char buf[1024];
      std::snprintf(buf, sizeof(buf), "Failed to add passphrase (index %zu): %s", index, archive_error_string(ar));
      throw_archive_fault(buf, ar);
    }
    ++index;
  }
}

static void configure_formats(struct archive *ar, const std::vector<std::string> &format_names) {
  if (format_names.empty()) {
    archive_read_support_format_all(ar);
    return;
  }

  using FormatHandler = int (*)(struct archive *);
  static const std::unordered_map<std::string, FormatHandler> kFormatHandlers = {
    { "7zip", archive_read_support_format_7zip },   { "ar", archive_read_support_format_ar },           { "cab", archive_read_support_format_cab }, { "cpio", archive_read_support_format_cpio },
    { "empty", archive_read_support_format_empty }, { "iso9660", archive_read_support_format_iso9660 }, { "lha", archive_read_support_format_lha }, { "mtree", archive_read_support_format_mtree },
    { "rar", archive_read_support_format_rar },     { "raw", archive_read_support_format_raw },         { "tar", archive_read_support_format_tar }, { "warc", archive_read_support_format_warc },
    { "xar", archive_read_support_format_xar },     { "zip", archive_read_support_format_zip },
  };

  for (const auto &format : format_names) {
    auto it = kFormatHandlers.find(format);
    if (it == kFormatHandlers.end()) {
      char buf[1024];
      std::snprintf(buf, sizeof(buf), "Unsupported archive format specified (%s)", format.c_str());
      throw_archive_fault(buf);
    }

    int r = it->second(ar);
    if (r != ARCHIVE_OK) {
      char buf[1024];
      std::snprintf(buf, sizeof(buf), "Failed to enable format (%s): %s", format.c_str(), archive_error_string(ar));
      throw_archive_fault(buf, ar);
    }
  }
}

static std::string format_archive_error(struct archive *ar, const std::string &prefix) {
  std::string message = prefix;

  if (!ar) {
    return message;
  }

  if (const char *err = archive_error_string(ar)) {
    if (*err) {
      message += ": ";
      message += err;
    }
  }

  const int code = archive_errno(ar);
  if (code != 0) {
    message += " (libarchive errno=";
    message += std::to_string(code);
    message += ')';
  }

  return message;
}

archive_ptr new_read_archive_common(const std::vector<std::string> &passphrases, const std::vector<std::string> &format_names, open_delegate archive_open) {
  archive_ptr ar(archive_read_new());
  if (!ar) {
    throw_archive_fault("archive_read_new failed.");
  }

  configure_formats(ar.get(), format_names);
  archive_read_support_filter_all(ar.get());
  set_passphrases(ar.get(), passphrases);

  if (archive_open(ar.get()) != ARCHIVE_OK) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), "archive_read_open failed: (%d)%s", archive_errno(ar.get()), archive_error_string(ar.get()));
    throw_archive_fault(buf, ar.get());
  }

  return ar;
}

Archive::Archive()
    : _ar(nullptr)
    , current_entry(nullptr)
    , _at_eof(false)
    , _current_entry_content_ready(false) {}

Archive::~Archive() { close_archive(); }

void Archive::close_archive() {
  if (_ar) {
    archive_read_close(_ar);
    archive_read_free(_ar);
    _ar = nullptr;
  }
  current_entry = nullptr;
  current_entryname.clear();
  _current_entry_content_ready = false;
}

void Archive::rewind() {
  close_archive();
  current_entryname.clear();
  current_entry = nullptr;
  _at_eof = false;
  open_archive();
  _current_entry_content_ready = false;
}

bool Archive::skip_to_next_header() {
  if (!_ar) {
    throw std::logic_error("Archive handle is not initialized");
  }

  if (_at_eof) {
    current_entry = nullptr;
    current_entryname.clear();
    _current_entry_content_ready = false;
    return false;
  }

  const int r = archive_read_next_header(_ar, &current_entry);

  if (r == ARCHIVE_EOF) {
    _at_eof = true;
    current_entry = nullptr;
    current_entryname.clear();
    _current_entry_content_ready = false;
    return false;
  }

  if (r == ARCHIVE_FAILED || r == ARCHIVE_FATAL || r == ARCHIVE_RETRY) {
    const std::string message = format_archive_error(_ar, "Failed to read next header");
    _at_eof = true;
    current_entry = nullptr;
    current_entryname.clear();
    _current_entry_content_ready = false;
    raise_archive_error(message);
  }

  const char *name = archive_entry_pathname(current_entry);
  if (name == nullptr) {
    throw make_entry_fault_error("Failed to retrieve entry pathname (archive_entry_pathname returned null)", {}, 0);
  }
  current_entryname = std::string(name);
  _current_entry_content_ready = true;
  return true;
}

bool Archive::skip_data() {
  if (!_ar) {
    throw std::logic_error("Archive handle is not initialized");
  }

  int r = archive_read_data_skip(_ar);
  if (r != ARCHIVE_OK) {
    raise_archive_error(format_archive_error(_ar, "Failed to skip data"));
  }
  _current_entry_content_ready = false;
  return true;
}

bool Archive::skip_to_entry(const std::string &entryname) {

  if (current_entryname == entryname && _current_entry_content_ready) {
    return true;
  }

  if (_at_eof) {
    rewind();
  }

  const std::string start_position = current_entryname;

  if (search_forward_until_eof(entryname)) {
    return true;
  }

  if (start_position.empty()) {
    return false;
  }

  rewind();

  return search_until_position(entryname, start_position);
}

bool Archive::skip_to_eof() {
  while (!_at_eof) {
    skip_to_next_header();
  }
  return true;
}

bool Archive::search_forward_until_eof(const std::string &entryname) {
  while (skip_to_next_header()) {
    if (current_entryname == entryname) {
      return true;
    }
    if (!skip_data()) {
      return false;
    }
  }
  return false;
}

bool Archive::search_until_position(const std::string &entryname, const std::string &stop_position) {
  while (skip_to_next_header()) {
    if (current_entryname == entryname) {
      return true;
    }
    if (current_entryname == stop_position) {
      break;
    }
    if (!skip_data()) {
      return false;
    }
  }
  return false;
}

ssize_t Archive::read_current(void *buff, size_t len) {
  if (!_ar) {
    throw std::logic_error("Archive handle is not initialized");
  }

  const ssize_t bytes_read = archive_read_data(_ar, buff, len);
  if (bytes_read < 0) {
    raise_archive_error(format_archive_error(_ar, "Failed to read data"));
  }

  _current_entry_content_ready = false;
  return bytes_read;
}

// Returns the current entry size in bytes, or 0 when no entry is selected.
uint64_t Archive::current_entry_size() const {
  if (!current_entry) {
    return 0;
  }
  return archive_entry_size(current_entry);
}

// Returns the current entry filetype bits, or 0 when no entry is selected.
mode_t Archive::current_entry_filetype() const {
  if (!current_entry) {
    return 0;
  }
  return archive_entry_filetype(current_entry);
}

EntryMetadataMap Archive::current_entry_metadata(const std::unordered_set<std::string> &allowed_keys) const {
  EntryMetadataMap metadata;
  if (!current_entry || allowed_keys.empty()) {
    return metadata;
  }

  auto wants = [&allowed_keys](const std::string &key) { return allowed_keys.find(key) != allowed_keys.end(); };

  const char *pathname_utf8 = archive_entry_pathname_utf8(current_entry);
  if (pathname_utf8 && *pathname_utf8 && wants("pathname")) {
    metadata["pathname"] = std::string(pathname_utf8);
  } else {
    const char *pathname = archive_entry_pathname(current_entry);
    if (pathname && *pathname && wants("pathname")) {
      metadata["pathname"] = std::string(pathname);
    }
  }

  if (const char *sourcepath = archive_entry_sourcepath(current_entry)) {
    if (wants("sourcepath")) {
      metadata["sourcepath"] = std::string(sourcepath);
    }
  }

  if (const char *symlink_utf8 = archive_entry_symlink_utf8(current_entry)) {
    if (wants("symlink")) {
      metadata["symlink"] = std::string(symlink_utf8);
    }
  }

  if (const char *hardlink_utf8 = archive_entry_hardlink_utf8(current_entry)) {
    if (wants("hardlink")) {
      metadata["hardlink"] = std::string(hardlink_utf8);
    }
  } else if (const char *hardlink = archive_entry_hardlink(current_entry)) {
    if (wants("hardlink")) {
      metadata["hardlink"] = std::string(hardlink);
    }
  }

  if (const char *uname_utf8 = archive_entry_uname_utf8(current_entry)) {
    if (wants("uname")) {
      metadata["uname"] = std::string(uname_utf8);
    }
  } else if (const char *uname = archive_entry_uname(current_entry)) {
    if (wants("uname")) {
      metadata["uname"] = std::string(uname);
    }
  }

  if (const char *gname_utf8 = archive_entry_gname_utf8(current_entry)) {
    if (wants("gname")) {
      metadata["gname"] = std::string(gname_utf8);
    }
  } else if (const char *gname = archive_entry_gname(current_entry)) {
    if (wants("gname")) {
      metadata["gname"] = std::string(gname);
    }
  }

  if (wants("uid")) {
    bool has_uid = archive_entry_uname(current_entry) != nullptr;
    if (!has_uid) {
      has_uid = archive_entry_uid(current_entry) != 0;
    }
    if (has_uid) {
      metadata["uid"] = static_cast<int64_t>(archive_entry_uid(current_entry));
    }
  }

  if (wants("gid")) {
    bool has_gid = archive_entry_gname(current_entry) != nullptr;
    if (!has_gid) {
      has_gid = archive_entry_gid(current_entry) != 0;
    }
    if (has_gid) {
      metadata["gid"] = static_cast<int64_t>(archive_entry_gid(current_entry));
    }
  }

  if (wants("perm")) {
    metadata["perm"] = static_cast<uint64_t>(archive_entry_perm(current_entry));
  }

  if (wants("mode")) {
    metadata["mode"] = static_cast<uint64_t>(archive_entry_mode(current_entry));
  }

  if (wants("filetype")) {
    metadata["filetype"] = static_cast<uint64_t>(archive_entry_filetype(current_entry));
  }

  if (archive_entry_size_is_set(current_entry) && wants("size")) {
    metadata["size"] = static_cast<uint64_t>(archive_entry_size(current_entry));
  }

  if (archive_entry_dev_is_set(current_entry) && wants("dev")) {
    metadata["dev"] = EntryMetadataDeviceNumbers{ static_cast<uint64_t>(archive_entry_devmajor(current_entry)), static_cast<uint64_t>(archive_entry_devminor(current_entry)) };
  }

  if (wants("rdev")) {
    const dev_t rdev = archive_entry_rdev(current_entry);
    if (rdev != 0) {
      metadata["rdev"] = EntryMetadataDeviceNumbers{ static_cast<uint64_t>(archive_entry_rdevmajor(current_entry)), static_cast<uint64_t>(archive_entry_rdevminor(current_entry)) };
    }
  }

  if (archive_entry_ino_is_set(current_entry)) {
    if (wants("ino")) {
      metadata["ino"] = static_cast<uint64_t>(archive_entry_ino(current_entry));
    }
    if (wants("ino64")) {
      metadata["ino64"] = static_cast<uint64_t>(archive_entry_ino64(current_entry));
    }
  }

  if (wants("nlink")) {
    metadata["nlink"] = static_cast<uint64_t>(archive_entry_nlink(current_entry));
  }

  if (const char *strmode = archive_entry_strmode(current_entry)) {
    if (wants("strmode")) {
      metadata["strmode"] = std::string(strmode);
    }
  }

  auto record_time = [&metadata, &wants](const char *key, bool is_set, time_t seconds, long nanoseconds) {
    if (!wants(key) || !is_set) {
      return;
    }
    metadata[key] = EntryMetadataTime{ static_cast<int64_t>(seconds), static_cast<int32_t>(nanoseconds) };
  };

  record_time("atime", archive_entry_atime_is_set(current_entry) != 0, archive_entry_atime(current_entry), archive_entry_atime_nsec(current_entry));
  record_time("birthtime", archive_entry_birthtime_is_set(current_entry) != 0, archive_entry_birthtime(current_entry), archive_entry_birthtime_nsec(current_entry));
  record_time("ctime", archive_entry_ctime_is_set(current_entry) != 0, archive_entry_ctime(current_entry), archive_entry_ctime_nsec(current_entry));
  record_time("mtime", archive_entry_mtime_is_set(current_entry) != 0, archive_entry_mtime(current_entry), archive_entry_mtime_nsec(current_entry));

  unsigned long fflags_set = 0;
  unsigned long fflags_clear = 0;
  archive_entry_fflags(current_entry, &fflags_set, &fflags_clear);
  if ((fflags_set != 0 || fflags_clear != 0) && wants("fflags")) {
    metadata["fflags"] = EntryMetadataFileFlags{ static_cast<uint64_t>(fflags_set), static_cast<uint64_t>(fflags_clear) };
  }

  if (const char *fflags_text = archive_entry_fflags_text(current_entry)) {
    if (wants("fflags_text")) {
      metadata["fflags_text"] = std::string(fflags_text);
    }
  }

  auto store_encryption_flag = [&metadata, &wants](const char *key, int value) {
    if (!wants(key) || value < 0) {
      return;
    }
    metadata[key] = (value != 0);
  };

  store_encryption_flag("is_data_encrypted", archive_entry_is_data_encrypted(current_entry));
  store_encryption_flag("is_metadata_encrypted", archive_entry_is_metadata_encrypted(current_entry));
  store_encryption_flag("is_encrypted", archive_entry_is_encrypted(current_entry));

  if (wants("symlink_type")) {
    const int symlink_type = archive_entry_symlink_type(current_entry);
    if (symlink_type != 0) {
      metadata["symlink_type"] = static_cast<int64_t>(symlink_type);
    }
  }

  ssize_t acl_length = 0;
  char *acl_text = wants("acl_text") ? archive_entry_acl_to_text(current_entry, &acl_length, ARCHIVE_ENTRY_ACL_STYLE_SEPARATOR_COMMA) : nullptr;
  if (acl_text) {
    if (acl_length >= 0) {
      metadata["acl_text"] = std::string(acl_text, static_cast<size_t>(acl_length));
    } else {
      metadata["acl_text"] = std::string(acl_text);
    }
    std::free(acl_text);
  }

  if (wants("acl_types")) {
    const int acl_types = archive_entry_acl_types(current_entry);
    if (acl_types != 0) {
      metadata["acl_types"] = static_cast<int64_t>(acl_types);
    }
  }

  const int xattr_count = archive_entry_xattr_count(current_entry);
  if (xattr_count > 0 && wants("xattr")) {
    std::vector<EntryMetadataXattr> xattrs;
    xattrs.reserve(static_cast<size_t>(xattr_count));
    archive_entry_xattr_reset(current_entry);
    const char *name = nullptr;
    const void *value = nullptr;
    size_t size = 0;
    while (archive_entry_xattr_next(current_entry, &name, &value, &size) == ARCHIVE_OK) {
      EntryMetadataXattr xattr;
      if (name) {
        xattr.name = name;
      }
      if (value && size > 0) {
        const auto *begin = static_cast<const uint8_t *>(value);
        xattr.value.assign(begin, begin + size);
      }
      xattrs.push_back(std::move(xattr));
    }
    if (!xattrs.empty()) {
      metadata["xattr"] = std::move(xattrs);
    }
  }

  const int sparse_count = archive_entry_sparse_count(current_entry);
  if (sparse_count > 0 && wants("sparse")) {
    std::vector<EntryMetadataSparseChunk> sparse_regions;
    sparse_regions.reserve(static_cast<size_t>(sparse_count));
    archive_entry_sparse_reset(current_entry);
    la_int64_t offset = 0;
    la_int64_t length = 0;
    while (archive_entry_sparse_next(current_entry, &offset, &length) == ARCHIVE_OK) {
      sparse_regions.push_back(EntryMetadataSparseChunk{ static_cast<int64_t>(offset), static_cast<int64_t>(length) });
    }
    if (!sparse_regions.empty()) {
      metadata["sparse"] = std::move(sparse_regions);
    }
  }

  if (wants("mac_metadata")) {
    size_t mac_metadata_size = 0;
    const void *mac_metadata = archive_entry_mac_metadata(current_entry, &mac_metadata_size);
    if (mac_metadata && mac_metadata_size > 0) {
      const auto *begin = static_cast<const uint8_t *>(mac_metadata);
      metadata["mac_metadata"] = std::vector<uint8_t>(begin, begin + mac_metadata_size);
    }
  }

  static constexpr struct {
    int type;
    const char *name;
    size_t length;
  } kDigestDescriptors[] = { { ARCHIVE_ENTRY_DIGEST_MD5, "md5", 16 },       { ARCHIVE_ENTRY_DIGEST_RMD160, "rmd160", 20 }, { ARCHIVE_ENTRY_DIGEST_SHA1, "sha1", 20 },
                             { ARCHIVE_ENTRY_DIGEST_SHA256, "sha256", 32 }, { ARCHIVE_ENTRY_DIGEST_SHA384, "sha384", 48 }, { ARCHIVE_ENTRY_DIGEST_SHA512, "sha512", 64 } };

  if (wants("digests")) {
    std::vector<EntryMetadataDigest> digests;
    for (const auto &descriptor : kDigestDescriptors) {
      const unsigned char *digest = archive_entry_digest(current_entry, descriptor.type);
      if (!digest) {
        continue;
      }
      EntryMetadataDigest digest_entry;
      digest_entry.algorithm = descriptor.name;
      digest_entry.value.assign(digest, digest + descriptor.length);
      digests.push_back(std::move(digest_entry));
    }
    if (!digests.empty()) {
      metadata["digests"] = std::move(digests);
    }
  }

  return metadata;
}

[[noreturn]] void Archive::raise_archive_error(const std::string &message) {
  const int err = _ar ? archive_errno(_ar) : 0;
  throw make_entry_fault_error(message, {}, err);
}

} // namespace archive_r
