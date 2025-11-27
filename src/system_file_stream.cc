// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "system_file_stream.h"
#include "archive_r/path_hierarchy_utils.h"
#include "entry_fault_error.h"

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <grp.h>
#include <pwd.h>
#include <stdexcept>
#include <sys/stat.h>
#include <system_error>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

namespace archive_r {

namespace {

static long determine_buffer_size(int name) {
  long size = ::sysconf(name);
  if (size < 0) {
    size = 16384; // Fallback for systems without a specific limit
  }
  return size;
}

static bool lookup_username(uid_t uid, std::string &name_out) {
  const long buf_size = determine_buffer_size(_SC_GETPW_R_SIZE_MAX);
  std::vector<char> buffer(static_cast<std::size_t>(buf_size));
  struct passwd pwd;
  struct passwd *result = nullptr;

  if (::getpwuid_r(uid, &pwd, buffer.data(), buffer.size(), &result) == 0 && result && result->pw_name) {
    name_out.assign(result->pw_name);
    return true;
  }
  return false;
}

static bool lookup_groupname(gid_t gid, std::string &name_out) {
  const long buf_size = determine_buffer_size(_SC_GETGR_R_SIZE_MAX);
  std::vector<char> buffer(static_cast<std::size_t>(buf_size));
  struct group grp;
  struct group *result = nullptr;

  if (::getgrgid_r(gid, &grp, buffer.data(), buffer.size(), &result) == 0 && result && result->gr_name) {
    name_out.assign(result->gr_name);
    return true;
  }
  return false;
}

} // namespace

SystemFileStream::SystemFileStream(PathHierarchy logical_path)
  : MultiVolumeStreamBase(std::move(logical_path), true)
  , _handle(nullptr) {
  if (_logical_path.empty()) {
    throw std::invalid_argument("Root file hierarchy cannot be empty");
  }

  const PathEntry &root_entry = _logical_path.front();
  if (!root_entry.is_single() && !root_entry.is_multi_volume()) {
    throw std::invalid_argument("Root file hierarchy must be a single file or multi-volume source");
  }
}

SystemFileStream::~SystemFileStream() = default;

void SystemFileStream::open_single_part(const PathHierarchy &single_part) {
  const PathEntry &entry = single_part.back();

  const std::string path = entry.single_value();
  errno = 0;
  FILE *handle = std::fopen(path.c_str(), "rb");
  if (!handle) {
    const int err = errno;
    throw make_entry_fault_error(format_path_errno_error("Failed to open root file", path, err), single_part, err);
  }

  _handle = handle;
  _active_path = path;
}

void SystemFileStream::close_single_part() {
  std::fclose(_handle);
  _handle = nullptr;
  _active_path.clear();
}

ssize_t SystemFileStream::read_from_single_part(void *buffer, size_t size) {
  errno = 0;
  const std::size_t bytes_read = std::fread(buffer, 1, size, _handle);
  if (bytes_read > 0) {
    return static_cast<ssize_t>(bytes_read);
  }

  if (std::feof(_handle)) {
    return 0;
  }

  if (std::ferror(_handle)) {
    report_read_failure(errno);
  }
  return -1;
}

int64_t SystemFileStream::seek_within_single_part(int64_t offset, int whence) {
  if (fseeko(_handle, offset, whence) != 0) {
    return -1;
  }
  const auto position = ftello(_handle);
  return position >= 0 ? position : -1;
}

int64_t SystemFileStream::size_of_single_part(const PathHierarchy &single_part) {
  const PathEntry &entry = single_part.back();

  struct stat st;
  if (::stat(entry.single_value().c_str(), &st) != 0) {
    return -1;
  }
  return static_cast<int64_t>(st.st_size);
}

void SystemFileStream::report_read_failure(int err) {
  const std::string detailed = format_path_errno_error("Failed to read root file", _active_path, err);
  close_single_part();
  throw make_entry_fault_error(detailed, _logical_path, err);
}

FilesystemMetadataInfo collect_root_path_metadata(const PathHierarchy &hierarchy, const std::unordered_set<std::string> &allowed_keys) {
  FilesystemMetadataInfo info;

  if (hierarchy.empty()) {
    return info;
  }

  std::error_code ec;
  const PathEntry &root_entry = hierarchy[0];
  if (!root_entry.is_single()) {
    return info;
  }

  const std::filesystem::path target(root_entry.single_value());
  std::filesystem::directory_entry entry(target, ec);
  if (ec) {
    return info;
  }

  mode_t filetype = 0;
  uint64_t size = 0;

  ec.clear();
  const bool is_regular = entry.is_regular_file(ec);
  if (!ec && is_regular) {
    ec.clear();
    size = entry.file_size(ec);
    if (ec) {
      size = 0;
    }
    filetype = S_IFREG;
  } else {
    ec.clear();
    const bool is_directory = entry.is_directory(ec);
    if (!ec && is_directory) {
      filetype = S_IFDIR;
    } else {
      ec.clear();
      const bool is_symlink = entry.is_symlink(ec);
      if (!ec && is_symlink) {
        filetype = S_IFLNK;
      }
    }
  }

  info.size = size;
  info.filetype = filetype;
  EntryMetadataMap metadata;
  if (!allowed_keys.empty()) {
    const auto wants = [&allowed_keys](std::string_view key) {
      return allowed_keys.find(std::string(key)) != allowed_keys.end();
    };

    // Path hierarchy / directory entry derived metadata
    if (wants("pathname")) {
      const PathEntry &tail = hierarchy.back();
      if (tail.is_single()) {
        metadata["pathname"] = tail.single_value();
      } else {
        metadata["pathname"] = path_entry_display(tail);
      }
    }

    if (wants("filetype")) {
      metadata["filetype"] = static_cast<uint64_t>(filetype);
    }

    if (wants("mode")) {
      std::error_code status_ec;
      const auto status = entry.status(status_ec);
      if (!status_ec) {
        metadata["mode"] = static_cast<uint64_t>(status.permissions());
      }
    }

    const bool wants_size = wants("size");
    const bool wants_uid = wants("uid");
    const bool wants_gid = wants("gid");
    const bool wants_uname = wants("uname");
    const bool wants_gname = wants("gname");
    const bool needs_stat = (wants_size && size == 0) || wants_uid || wants_gid || wants_uname || wants_gname;

    struct stat stat_buffer;
    bool have_stat = false;
    if (needs_stat) {
      const std::string native_path = entry.path().string();
      have_stat = (::stat(native_path.c_str(), &stat_buffer) == 0);
    }

    if (wants_size) {
      uint64_t resolved = size;
      if (resolved == 0 && have_stat) {
        resolved = static_cast<uint64_t>(stat_buffer.st_size);
      }
      if (resolved > 0 || (size == 0 && have_stat)) {
        metadata["size"] = resolved;
      }
    }

    if (have_stat) {
      if (wants_uid) {
        metadata["uid"] = static_cast<int64_t>(stat_buffer.st_uid);
      }
      if (wants_gid) {
        metadata["gid"] = static_cast<int64_t>(stat_buffer.st_gid);
      }
      if (wants_uname) {
        std::string uname;
        if (lookup_username(stat_buffer.st_uid, uname)) {
          metadata["uname"] = std::move(uname);
        }
      }
      if (wants_gname) {
        std::string gname;
        if (lookup_groupname(stat_buffer.st_gid, gname)) {
          metadata["gname"] = std::move(gname);
        }
      }
    }
  }

  info.metadata = std::move(metadata);
  return info;
}

} // namespace archive_r
