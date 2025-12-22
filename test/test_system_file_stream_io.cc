// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "system_file_stream.h"
#include "entry_fault_error.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_WIN32)
#  include <unistd.h>
#else
#  include <process.h>
#endif

using namespace archive_r;

namespace {

bool expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << std::endl;
    return false;
  }
  return true;
}

std::string make_temp_file_with_content() {
  const auto pid =
#if defined(_WIN32)
      _getpid();
#else
      getpid();
#endif

  const std::filesystem::path base = std::filesystem::path("build") / "tmp_system_file_stream" / std::to_string(static_cast<long long>(pid));
  std::error_code ec;
  std::filesystem::create_directories(base, ec);

  const std::filesystem::path file_path = base / "stream_test.bin";
  {
    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    out << "0123456789ABCDEF";
  }
  return file_path.string();
}

std::string write_temp_file(const std::string &name, const std::string &content) {
  const auto pid =
#if defined(_WIN32)
      _getpid();
#else
      getpid();
#endif

  const std::filesystem::path base = std::filesystem::path("build") / "tmp_system_file_stream" / std::to_string(static_cast<long long>(pid));
  std::error_code ec;
  std::filesystem::create_directories(base, ec);

  const std::filesystem::path file_path = base / name;
  {
    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      throw std::runtime_error("Failed to open temp file for writing");
    }
    out << content;
  }
  return file_path.string();
}

std::string make_temp_dir(const std::string &name) {
  const auto pid =
#if defined(_WIN32)
      _getpid();
#else
      getpid();
#endif
  const std::filesystem::path base = std::filesystem::path("build") / "tmp_system_file_stream" / std::to_string(static_cast<long long>(pid));
  std::error_code ec;
  std::filesystem::create_directories(base, ec);
  const std::filesystem::path dir_path = base / name;
  std::filesystem::create_directories(dir_path, ec);
  return dir_path.string();
}

} // namespace

int main() {
  bool ok = true;

  // Constructor should reject empty hierarchy.
  {
    bool threw = false;
    try {
      (void)std::make_shared<SystemFileStream>(PathHierarchy{});
    } catch (const std::invalid_argument &) {
      threw = true;
    }
    ok = ok && expect(threw, "SystemFileStream(empty hierarchy) should throw std::invalid_argument");
  }

  // Basic read/seek/tell on an existing file.
  const std::string path = make_temp_file_with_content();
  try {
    auto stream = std::make_shared<SystemFileStream>(make_single_path(path));

    char buf[6] = {0};
    const ssize_t n1 = stream->read(buf, 5);
    ok = ok && expect(n1 == 5, "read should return 5 bytes");
    ok = ok && expect(std::string(buf, 5) == "01234", "read content mismatch");

    const int64_t pos = stream->tell();
    ok = ok && expect(pos >= 5, "tell should be >= 5 after read");

    const int64_t seek_pos = stream->seek(0, SEEK_SET);
    ok = ok && expect(seek_pos == 0, "seek(SEEK_SET,0) should return 0");

    std::memset(buf, 0, sizeof(buf));
    const ssize_t n2 = stream->read(buf, 5);
    ok = ok && expect(n2 == 5, "second read should return 5 bytes");
    ok = ok && expect(std::string(buf, 5) == "01234", "second read content mismatch");

    // Read to EOF
    char eof_buf[20] = {0};
    const ssize_t n3 = stream->read(eof_buf, sizeof(eof_buf));
    ok = ok && expect(n3 == 11, "read to EOF should return remaining bytes"); // "0123456789ABCDEF" is 16 bytes, already read 5, remaining 11
    ok = ok && expect(std::string(eof_buf, 11) == "56789ABCDEF", "EOF read content mismatch");

    // Read past EOF should return 0
    const ssize_t n4 = stream->read(eof_buf, sizeof(eof_buf));
    ok = ok && expect(n4 == 0, "read past EOF should return 0");

  } catch (const std::exception &ex) {
    std::cerr << "Unexpected failure reading existing file: " << ex.what() << std::endl;
    ok = false;
  }

  // Open failure path: non-existent file should throw EntryFaultError on first read.
  try {
    auto stream = std::make_shared<SystemFileStream>(make_single_path("build/tmp_system_file_stream/does_not_exist.bin"));
    char buf[1];
    (void)stream->read(buf, sizeof(buf));
    std::cerr << "Expected exception when opening missing file" << std::endl;
    ok = false;
  } catch (const EntryFaultError &) {
    // expected
  } catch (const std::exception &ex) {
    std::cerr << "Unexpected exception type: " << ex.what() << std::endl;
    ok = false;
  }

  // Seek corner cases: invalid whence and out-of-range positions.
  try {
    const std::string p = make_temp_file_with_content();
    auto stream = std::make_shared<SystemFileStream>(make_single_path(p));
    ok = ok && expect(stream->seek(0, 12345) == -1, "seek with invalid whence should return -1");
    ok = ok && expect(stream->seek(999999, SEEK_SET) == -1, "seek beyond EOF should return -1");
  } catch (const std::exception &ex) {
    std::cerr << "Unexpected failure in seek corner cases: " << ex.what() << std::endl;
    ok = false;
  }

  // Multi-volume read/seek should work across part boundaries.
  try {
    const std::string part1 = write_temp_file("mv_part001.bin", "ABC");
    const std::string part2 = write_temp_file("mv_part002.bin", "DEF");

    PathHierarchy h;
    h.emplace_back(PathEntry::multi_volume({part1, part2}));
    auto stream = std::make_shared<SystemFileStream>(h);

    char buf[8] = {0};
    const ssize_t n = stream->read(buf, 6);
    ok = ok && expect(n == 6, "multi-volume read should return 6 bytes");
    ok = ok && expect(std::string(buf, 6) == "ABCDEF", "multi-volume read content mismatch");

    ok = ok && expect(stream->tell() == 6, "tell after multi-volume read should be 6");

    ok = ok && expect(stream->seek(0, SEEK_SET) == 0, "seek(SEEK_SET,0) should return 0");
    std::memset(buf, 0, sizeof(buf));
    ok = ok && expect(stream->read(buf, 3) == 3, "multi-volume read 3 bytes should succeed");
    ok = ok && expect(std::string(buf, 3) == "ABC", "multi-volume first-part content mismatch");

    ok = ok && expect(stream->seek(-3, SEEK_END) == 3, "seek(-3,SEEK_END) should land at offset 3");
    std::memset(buf, 0, sizeof(buf));
    ok = ok && expect(stream->read(buf, 3) == 3, "multi-volume read from end should succeed");
    ok = ok && expect(std::string(buf, 3) == "DEF", "multi-volume second-part content mismatch");

    ok = ok && expect(stream->seek(0, SEEK_END) == 6, "seek(0,SEEK_END) should return total size");
    ok = ok && expect(stream->at_end(), "at_end() should be true after seek to end");
    ok = ok && expect(stream->read(buf, 1) == 0, "read at end should return 0");

  } catch (const std::exception &ex) {
    std::cerr << "Unexpected failure in multi-volume IO test: " << ex.what() << std::endl;
    ok = false;
  }

  // Multi-volume error paths: missing part should fail seek and fail when advancing.
  try {
    const std::string part1 = write_temp_file("mv_missing_part001.bin", "XYZ");
    const std::string missing_part2 = std::filesystem::path(part1).parent_path().append("mv_missing_part002.bin").string();

    PathHierarchy h;
    h.emplace_back(PathEntry::multi_volume({part1, missing_part2}));
    auto stream = std::make_shared<SystemFileStream>(h);

    ok = ok && expect(stream->seek(0, SEEK_END) == -1, "seek(SEEK_END) should fail when a part is missing");

    char buf[8] = {0};
    const ssize_t n1 = stream->read(buf, 3);
    ok = ok && expect(n1 == 3, "first part should read successfully");

    bool threw = false;
    try {
      (void)stream->read(buf, sizeof(buf));
    } catch (const EntryFaultError &) {
      threw = true;
    }
    ok = ok && expect(threw, "reading past first part should throw when next part is missing");

  } catch (const std::exception &ex) {
    std::cerr << "Unexpected failure in missing-part test: " << ex.what() << std::endl;
    ok = false;
  }

  // Directory-as-file should fail (either open or read) with EntryFaultError.
  try {
    const std::string dir = make_temp_dir("dir_as_file");
    auto stream = std::make_shared<SystemFileStream>(make_single_path(dir));
    char buf[1] = {0};
    bool threw = false;
    try {
      (void)stream->read(buf, sizeof(buf));
    } catch (const EntryFaultError &) {
      threw = true;
    }
    ok = ok && expect(threw, "directory path should fail as a file stream");
  } catch (const std::exception &ex) {
    std::cerr << "Unexpected failure in directory-as-file test: " << ex.what() << std::endl;
    ok = false;
  }

  if (!ok) {
    return 1;
  }

  std::cout << "SystemFileStream IO tests passed" << std::endl;
  return 0;
}
