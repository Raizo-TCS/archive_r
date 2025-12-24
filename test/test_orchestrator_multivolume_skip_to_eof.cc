// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/traverser.h"

#include <archive.h>
#include <archive_entry.h>

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace archive_r;

namespace {

bool expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << std::endl;
    return false;
  }
  return true;
}

std::vector<uint8_t> load_file_bytes(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("Failed to open file: " + path);
  }
  in.seekg(0, std::ios::end);
  const auto size = in.tellg();
  if (size < 0) {
    throw std::runtime_error("Failed to determine file size: " + path);
  }
  in.seekg(0, std::ios::beg);

  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  if (!bytes.empty()) {
    in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!in) {
      throw std::runtime_error("Failed to read bytes from: " + path);
    }
  }
  return bytes;
}

void write_outer_tar(const std::filesystem::path &out_path, const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files) {
  struct archive *a = archive_write_new();
  if (!a) {
    throw std::runtime_error("archive_write_new failed");
  }

  archive_write_set_format_pax_restricted(a);

  if (archive_write_open_filename(a, out_path.string().c_str()) != ARCHIVE_OK) {
    std::string msg = archive_error_string(a) ? archive_error_string(a) : "(null)";
    archive_write_free(a);
    throw std::runtime_error(std::string("archive_write_open_filename failed: ") + msg);
  }

  for (const auto &[name, content] : files) {
    struct archive_entry *entry = archive_entry_new();
    archive_entry_set_pathname(entry, name.c_str());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, static_cast<la_int64_t>(content.size()));

    if (archive_write_header(a, entry) != ARCHIVE_OK) {
      std::string msg = archive_error_string(a) ? archive_error_string(a) : "(null)";
      archive_entry_free(entry);
      archive_write_close(a);
      archive_write_free(a);
      throw std::runtime_error(std::string("archive_write_header failed: ") + msg);
    }

    const uint8_t *p = content.data();
    size_t remaining = content.size();
    while (remaining > 0) {
      const la_ssize_t w = archive_write_data(a, p, remaining);
      if (w < 0) {
        std::string msg = archive_error_string(a) ? archive_error_string(a) : "(null)";
        archive_entry_free(entry);
        archive_write_close(a);
        archive_write_free(a);
        throw std::runtime_error(std::string("archive_write_data failed: ") + msg);
      }
      if (w == 0) {
        archive_entry_free(entry);
        archive_write_close(a);
        archive_write_free(a);
        throw std::runtime_error("archive_write_data wrote 0 bytes");
      }
      p += static_cast<size_t>(w);
      remaining -= static_cast<size_t>(w);
    }

    archive_entry_free(entry);
  }

  archive_write_close(a);
  archive_write_free(a);
}

struct TempDir {
  std::filesystem::path path;

  TempDir() {
    const auto base = std::filesystem::temp_directory_path();
    const auto candidate = base / "archive_r_test_orch_mv_skip_to_eof";
    std::error_code ec;
    std::filesystem::remove_all(candidate, ec);
    std::filesystem::create_directories(candidate, ec);
    if (ec) {
      throw std::runtime_error("Failed to create temp directory: " + ec.message());
    }
    path = candidate;
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

bool hierarchy_contains_multivolume(const PathHierarchy &hier) {
  for (const auto &component : hier) {
    if (component.is_multi_volume()) {
      return true;
    }
  }
  return false;
}

} // namespace

int main() {
  bool ok = true;

  TempDir temp;
  const auto outer_tar = temp.path / "outer.tar";

  const std::string base_name = "test_input.tar.gz";

  std::vector<std::pair<std::string, std::vector<uint8_t>>> outer_files;
  outer_files.reserve(5);

  for (int i = 0; i < 5; ++i) {
    char suffix[64];
    std::snprintf(suffix, sizeof(suffix), "test_input.tar.gz.part%02d", i);
    const std::string entry_name = suffix;
    const std::string src_path = std::string("test_data/") + entry_name;
    outer_files.emplace_back(entry_name, load_file_bytes(src_path));
  }

  try {
    write_outer_tar(outer_tar, outer_files);
  } catch (const std::exception &ex) {
    std::cerr << "Failed to create outer tar: " << ex.what() << std::endl;
    return 1;
  }

  TraverserOptions opts;
  opts.descend_archives = true;
  opts.formats = { "tar" }; // avoid 'raw' so part files are not treated as standalone archives

  int parts_marked = 0;
  bool saw_resolved_multivolume = false;
  bool saw_any_inner_entry = false;

  try {
    Traverser t(outer_tar.string(), opts);
    for (Entry &entry : t) {
      const auto &hier = entry.path_hierarchy();
      if (hierarchy_contains_multivolume(hier)) {
        saw_resolved_multivolume = true;
        if (entry.depth() >= 2) {
          saw_any_inner_entry = true;
        }
      }

      const std::string name = entry.name();
      if (name.rfind("test_input.tar.gz.part", 0) == 0) {
        entry.set_multi_volume_group(base_name);
        ++parts_marked;
      }
    }
  } catch (const std::exception &ex) {
    std::cerr << "Unexpected exception during traversal: " << ex.what() << std::endl;
    return 1;
  }

  ok = expect(parts_marked == 5, "Expected to mark 5 multi-volume parts, got: " + std::to_string(parts_marked)) && ok;
  ok = expect(saw_resolved_multivolume, "Expected to observe resolved multi-volume hierarchy during traversal") && ok;
  ok = expect(saw_any_inner_entry, "Expected to traverse at least one entry inside resolved multi-volume archive") && ok;

  if (ok) {
    std::cout << "Orchestrator multi-volume skip_to_eof coverage test passed" << std::endl;
    return 0;
  }

  return 1;
}
