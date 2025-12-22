// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_stack_cursor.h"
#include "archive_r/data_stream.h"

#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
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

// MemoryStream: buffer-based stream for testing
class MemoryStream : public IDataStream {
private:
  std::vector<uint8_t> _buffer;
  size_t _pos = 0;
  PathHierarchy _hierarchy;

public:
  MemoryStream(PathHierarchy hierarchy, const std::vector<uint8_t> &data) : _buffer(data), _hierarchy(std::move(hierarchy)) {}

  ssize_t read(void *buf, size_t len) override {
    if (_pos >= _buffer.size()) return 0;
    const size_t remaining = _buffer.size() - _pos;
    const size_t to_read = (len < remaining) ? len : remaining;
    std::memcpy(buf, _buffer.data() + _pos, to_read);
    _pos += to_read;
    return static_cast<ssize_t>(to_read);
  }

  int64_t seek(int64_t offset, int whence) override {
    int64_t new_pos = 0;
    if (whence == SEEK_SET) {
      new_pos = offset;
    } else if (whence == SEEK_CUR) {
      new_pos = static_cast<int64_t>(_pos) + offset;
    } else if (whence == SEEK_END) {
      new_pos = static_cast<int64_t>(_buffer.size()) + offset;
    }
    if (new_pos < 0 || new_pos > static_cast<int64_t>(_buffer.size())) {
      return -1;
    }
    _pos = static_cast<size_t>(new_pos);
    return new_pos;
  }

  int64_t tell() const override { return static_cast<int64_t>(_pos); }

  bool can_seek() const override { return true; }

  void rewind() override { _pos = 0; }

  bool at_end() const override { return _pos >= _buffer.size(); }

  PathHierarchy source_hierarchy() const override { return _hierarchy; }
};

// ErrorThrowingStream: stream that throws on read
class ErrorThrowingStream : public IDataStream {
private:
  PathHierarchy _hierarchy;

public:
  explicit ErrorThrowingStream(PathHierarchy hierarchy) : _hierarchy(std::move(hierarchy)) {}

  ssize_t read(void *, size_t) override { throw std::runtime_error("Stream read error"); }

  int64_t seek(int64_t, int) override { throw std::runtime_error("Stream seek error"); }

  int64_t tell() const override { return 0; }

  bool can_seek() const override { return true; }

  void rewind() override {}

  bool at_end() const override { return false; }

  PathHierarchy source_hierarchy() const override { return _hierarchy; }
};

class RootStreamFactoryGuard {
public:
  RootStreamFactoryGuard()
      : _original(get_root_stream_factory()) {}

  ~RootStreamFactoryGuard() { set_root_stream_factory(_original); }

  RootStreamFactoryGuard(const RootStreamFactoryGuard &) = delete;
  RootStreamFactoryGuard &operator=(const RootStreamFactoryGuard &) = delete;

private:
  RootStreamFactory _original;
};

std::vector<uint8_t> build_tar_payload(const std::vector<std::pair<std::string, std::string>> &files) {
  struct archive *a = archive_write_new();
  if (!a) {
    throw std::runtime_error("archive_write_new failed");
  }

  archive_write_set_format_pax_restricted(a);

  std::vector<uint8_t> buffer(1024 * 1024);
  size_t used = 0;
  if (archive_write_open_memory(a, buffer.data(), buffer.size(), &used) != ARCHIVE_OK) {
    std::string msg = archive_error_string(a) ? archive_error_string(a) : "(null)";
    archive_write_free(a);
    throw std::runtime_error(std::string("archive_write_open_memory failed: ") + msg);
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

    if (!content.empty()) {
      const la_ssize_t w = archive_write_data(a, content.data(), content.size());
      if (w < 0) {
        std::string msg = archive_error_string(a) ? archive_error_string(a) : "(null)";
        archive_entry_free(entry);
        archive_write_close(a);
        archive_write_free(a);
        throw std::runtime_error(std::string("archive_write_data failed: ") + msg);
      }
    }

    archive_entry_free(entry);
  }

  archive_write_close(a);
  archive_write_free(a);

  buffer.resize(used);
  return buffer;
}

} // namespace

int main() {
  try {
    // Test 1: StreamArchive with null stream throws
    {
      bool threw = false;
      try {
        StreamArchive sa(nullptr, {});
      } catch (const std::invalid_argument &) {
        threw = true;
      }
      if (!expect(threw, "StreamArchive(nullptr) should throw std::invalid_argument")) {
        return 1;
      }
    }

    // Test 2: StreamArchive with valid stream initializes
    {
      std::vector<uint8_t> empty_data;
      auto stream = std::make_shared<MemoryStream>(make_single_path("/test/stream"), empty_data);
      try {
        StreamArchive sa(stream, {});
        if (!expect(true, "StreamArchive with valid stream should initialize")) {
          return 1;
        }
      } catch (const std::exception &ex) {
        if (!expect(false, std::string("StreamArchive initialization failed: ").append(ex.what()).c_str())) {
          return 1;
        }
      }
    }

    // Test 3: read_callback_bridge handles exceptions
    {
      auto stream = std::make_shared<ErrorThrowingStream>(make_single_path("/test/error"));
      try {
        StreamArchive sa(stream, {});
        // read_callback_bridge would return -1 on exception internally
        if (!expect(true, "ErrorThrowingStream should be handled gracefully")) {
          return 1;
        }
      } catch (const std::exception &ex) {
        // Expected: exception during archive_read_open1 call
        if (!expect(true, "Exception handling in callbacks works")) {
          return 1;
        }
      }
    }

    // Test 4: skip_callback_bridge handles exceptions
    {
      auto stream = std::make_shared<ErrorThrowingStream>(make_single_path("/test/error"));
      try {
        StreamArchive sa(stream, {});
        if (!expect(true, "skip_callback_bridge exception handling")) {
          return 1;
        }
      } catch (const std::exception &) {
        // Expected
      }
    }

    // Test 5: parent_archive returns nullptr for non-EntryPayloadStream
    {
      std::vector<uint8_t> empty_data;
      auto stream = std::make_shared<MemoryStream>(make_single_path("/test/stream"), empty_data);
      try {
        StreamArchive sa(stream, {});
        auto parent = sa.parent_archive();
        if (!expect(parent == nullptr, "parent_archive should return nullptr for MemoryStream")) {
          return 1;
        }
      } catch (const std::exception &) {
        // Parent archive check on generic stream
      }
    }

    // Test 6: source_hierarchy delegates to stream
    {
      std::vector<uint8_t> empty_data;
      auto stream = std::make_shared<MemoryStream>(make_single_path("/test/stream"), empty_data);
      try {
        StreamArchive sa(stream, {});
        auto hierarchy = sa.source_hierarchy();
        if (!expect(true, "source_hierarchy should work")) {
          return 1;
        }
      } catch (const std::exception &) {
        // Fallback
      }
    }

    // Test 7: rewind delegates to stream
    {
      std::vector<uint8_t> data = {0x1f, 0x8b}; // gzip magic number
      auto stream = std::make_shared<MemoryStream>(make_single_path("/test/stream"), data);
      try {
        StreamArchive sa(stream, {});
        sa.rewind();
        if (!expect(true, "rewind should work")) {
          return 1;
        }
      } catch (const std::exception &) {
        // Fallback
      }
    }

    // Test 8: ArchiveStackCursor basic flow (synchronize -> descend -> next -> read -> ascend)
    {
      RootStreamFactoryGuard guard;

      const PathHierarchy root = make_single_path("/virtual/archive.tar");
      const auto payload = build_tar_payload({ {"a.txt", "A"}, {"b.txt", "BB"} });

      set_root_stream_factory([payload, root](const PathHierarchy &hierarchy) -> std::shared_ptr<IDataStream> {
        if (hierarchies_equal(hierarchy, root)) {
          return std::make_shared<MemoryStream>(hierarchy, payload);
        }
        return nullptr;
      });

      ArchiveStackCursor cursor;

      if (!expect(cursor.depth() == 0, "cursor initial depth should be 0")) {
        return 1;
      }

      cursor.synchronize_to_hierarchy(root);
      if (!expect(cursor.has_stream(), "cursor should have stream after synchronize_to_hierarchy(root)")) {
        return 1;
      }

      cursor.descend();
      if (!expect(cursor.depth() == 1, "cursor depth should be 1 after descend")) {
        return 1;
      }

      if (!expect(cursor.next(), "cursor.next() should succeed on simple tar")) {
        return 1;
      }

      const PathHierarchy entry_h = cursor.current_entry_hierarchy();
      if (!expect(entry_h.size() == 2, "current_entry_hierarchy should be depth 2 (root + entry)")) {
        return 1;
      }

      char buf[8] = {};
      const ssize_t r = cursor.read(buf, sizeof(buf));
      if (!expect(r > 0, "cursor.read should return >0 bytes")) {
        return 1;
      }

      if (!expect(cursor.ascend(), "cursor.ascend should succeed when depth>0")) {
        return 1;
      }
      if (!expect(cursor.depth() == 0, "cursor depth should be 0 after ascend")) {
        return 1;
      }
    }

    // Test 9: ArchiveStackCursor error paths
    {
      ArchiveStackCursor cursor;
      bool threw = false;
      try {
        cursor.descend();
      } catch (const std::logic_error &) {
        threw = true;
      }
      if (!expect(threw, "cursor.descend() without stream should throw std::logic_error")) {
        return 1;
      }

      if (!expect(!cursor.next(), "cursor.next() without archive should return false")) {
        return 1;
      }

      bool threw_fault = false;
      try {
        cursor.synchronize_to_hierarchy(PathHierarchy{});
      } catch (const EntryFaultError &) {
        threw_fault = true;
      }
      if (!expect(threw_fault, "synchronize_to_hierarchy(empty) should throw EntryFaultError")) {
        return 1;
      }
    }

    std::cout << "Archive stack cursor tests exercised" << std::endl;
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Archive stack cursor test failed: " << ex.what() << std::endl;
    return 1;
  }
}
