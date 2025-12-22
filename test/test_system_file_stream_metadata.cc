// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "system_file_stream.h"

// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "system_file_stream.h"

#include "archive_r/path_hierarchy.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#if !defined(_WIN32)
#  include <unistd.h>
#else
#  include <process.h>
#endif

using namespace archive_r;

namespace {

bool expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << std::endl;
    return false;
  }
  return true;
}

std::filesystem::path unique_temp_dir() {
  const auto base = std::filesystem::temp_directory_path();
  const auto pid =
#if defined(_WIN32)
      _getpid();
#else
      getpid();
#endif
  const auto candidate = base / ("archive_r_test_sysmeta_" + std::to_string(static_cast<long long>(pid)));
  std::error_code ec;
  std::filesystem::remove_all(candidate, ec);
  std::filesystem::create_directories(candidate, ec);
  if (ec) {
    throw std::runtime_error("Failed to create temp directory: " + ec.message());
  }
  return candidate;
}

struct TempDir {
  std::filesystem::path path;
  TempDir() : path(unique_temp_dir()) {}
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

} // namespace

int main() {
  bool ok = true;

  {
    FilesystemMetadataInfo empty = collect_root_path_metadata(PathHierarchy{}, std::unordered_set<std::string>{"pathname"});
    ok = expect(empty.metadata.empty(), "Expected empty metadata for empty hierarchy") && ok;
  }

  {
    PathHierarchy non_single_root;
    non_single_root.emplace_back(PathEntry::multi_volume({"a", "b"}));
    FilesystemMetadataInfo info = collect_root_path_metadata(non_single_root, std::unordered_set<std::string>{"pathname"});
    ok = expect(info.metadata.empty(), "Expected empty metadata when root is not a single path") && ok;
  }

  TempDir temp;
  const auto root = temp.path;
  const auto regular_path = root / "file.txt";
  const auto dir_path = root / "dir";
  const auto missing_path = root / "missing.txt";
  const auto link_path = root / "link.txt";

  std::error_code ec;
  std::filesystem::create_directories(dir_path, ec);
  if (ec) {
    std::cerr << "Failed to create directory: " << ec.message() << std::endl;
    return 1;
  }

  {
    std::ofstream out(regular_path);
    out << "hello";
  }

  {
    PathHierarchy h;
    h.emplace_back(PathEntry::single(regular_path.string()));

    std::unordered_set<std::string> keys = {"pathname", "filetype", "mode", "size"};
#if !defined(_WIN32)
    keys.insert("uid");
    keys.insert("gid");
    keys.insert("uname");
    keys.insert("gname");
#endif

    FilesystemMetadataInfo info = collect_root_path_metadata(h, keys);

    ok = expect(!info.metadata.empty(), "Expected metadata for regular file") && ok;
    ok = expect(info.metadata.find("pathname") != info.metadata.end(), "Expected pathname metadata") && ok;
    ok = expect(info.metadata.find("filetype") != info.metadata.end(), "Expected filetype metadata") && ok;
    ok = expect(info.metadata.find("mode") != info.metadata.end(), "Expected mode metadata") && ok;
    ok = expect(info.metadata.find("size") != info.metadata.end(), "Expected size metadata") && ok;
  }

  // allowed_keys empty: metadata should be empty, but size/filetype should still be populated.
  {
    PathHierarchy h;
    h.emplace_back(PathEntry::single(regular_path.string()));
    FilesystemMetadataInfo info = collect_root_path_metadata(h, std::unordered_set<std::string>{});
    ok = expect(info.metadata.empty(), "Expected empty metadata when allowed_keys is empty") && ok;
    ok = expect(info.filetype != 0, "Expected filetype to be set even when allowed_keys is empty") && ok;
    ok = expect(info.size == 5, "Expected size to be set for regular file even when allowed_keys is empty") && ok;
  }

  {
    PathHierarchy h;
    h.emplace_back(PathEntry::single(dir_path.string()));

    FilesystemMetadataInfo info = collect_root_path_metadata(h, std::unordered_set<std::string>{"filetype", "size"});
    ok = expect(info.metadata.find("filetype") != info.metadata.end(), "Expected filetype metadata for directory") && ok;
    ok = expect(info.metadata.find("size") != info.metadata.end(), "Expected size metadata for directory (stat-derived)") && ok;
  }

  // size metadata should be present even for empty files (size==0) when stat succeeds.
  {
    const auto empty_path = root / "empty.txt";
    {
      std::ofstream out(empty_path, std::ios::binary | std::ios::trunc);
    }

    PathHierarchy h;
    h.emplace_back(PathEntry::single(empty_path.string()));
    FilesystemMetadataInfo info = collect_root_path_metadata(h, std::unordered_set<std::string>{"size"});
    const auto it = info.metadata.find("size");
    ok = expect(it != info.metadata.end(), "Expected size metadata for empty file") && ok;
    if (it != info.metadata.end()) {
      ok = expect(std::get<uint64_t>(it->second) == 0, "Expected size==0 for empty file") && ok;
    }
  }

  {
    PathHierarchy h;
    h.emplace_back(PathEntry::single(missing_path.string()));

    FilesystemMetadataInfo info = collect_root_path_metadata(h, std::unordered_set<std::string>{"pathname", "filetype", "size"});
    ok = expect(info.metadata.empty(), "Expected empty metadata for missing path") && ok;
  }

  {
    PathHierarchy h;
    h.emplace_back(PathEntry::single(regular_path.string()));
    h.emplace_back(PathEntry::multi_volume({"part1", "part2"}));

    FilesystemMetadataInfo info = collect_root_path_metadata(h, std::unordered_set<std::string>{"pathname"});
    const auto it = info.metadata.find("pathname");
    ok = expect(it != info.metadata.end(), "Expected pathname metadata for multi-volume tail") && ok;
    if (it != info.metadata.end()) {
      ok = expect(std::get<std::string>(it->second) == "[part1|part2]", "Expected multi-volume pathname rendering") && ok;
    }
  }

  {
    try {
      std::filesystem::create_symlink(regular_path, link_path);
      PathHierarchy h;
      h.emplace_back(PathEntry::single(link_path.string()));
      FilesystemMetadataInfo info = collect_root_path_metadata(h, std::unordered_set<std::string>{"filetype", "size"});
      ok = expect(!info.metadata.empty(), "Expected metadata for symlink path") && ok;
    } catch (const std::exception &) {
      // Symlink creation may be disallowed; skip.
    }
  }

  if (!ok) {
    return 1;
  }

  std::cout << "SystemFileStream metadata tests passed" << std::endl;
  return 0;
}
      // Symlink creation may be disallowed; skip.
