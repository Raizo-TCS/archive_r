// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "system_file_stream.h"
#include "entry_fault_error.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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

std::string make_temp_file_with_content() {
  const std::filesystem::path base = std::filesystem::path("build") / "tmp_system_file_stream";
  std::error_code ec;
  std::filesystem::create_directories(base, ec);

  const std::filesystem::path file_path = base / "stream_test.bin";
  {
    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    out << "0123456789ABCDEF";
  }
  return file_path.string();
}

} // namespace

int main() {
  bool ok = true;

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

  if (!ok) {
    return 1;
  }

  std::cout << "SystemFileStream IO tests passed" << std::endl;
  return 0;
}
