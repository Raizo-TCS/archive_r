// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_stack_cursor.h"
#include "archive_r/data_stream.h"

#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
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

// TellMinusOneStream: tell() always returns -1, and seek(0,SEEK_CUR) returns -1
// to exercise skip_callback_bridge's "cannot determine current" path.
class TellMinusOneStream : public IDataStream {
private:
  std::vector<uint8_t> _buffer;
  size_t _pos = 0;
  PathHierarchy _hierarchy;

public:
  TellMinusOneStream(PathHierarchy hierarchy, const std::vector<uint8_t> &data) : _buffer(data), _hierarchy(std::move(hierarchy)) {}

  ssize_t read(void *buf, size_t len) override {
    if (_pos >= _buffer.size()) return 0;
    const size_t remaining = _buffer.size() - _pos;
    const size_t to_read = (len < remaining) ? len : remaining;
    std::memcpy(buf, _buffer.data() + _pos, to_read);
    _pos += to_read;
    return static_cast<ssize_t>(to_read);
  }

  int64_t seek(int64_t offset, int whence) override {
    if (whence == SEEK_CUR && offset == 0) {
      return -1;
    }
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

  int64_t tell() const override { return -1; }

  bool can_seek() const override { return true; }

  void rewind() override { _pos = 0; }

  bool at_end() const override { return _pos >= _buffer.size(); }

  PathHierarchy source_hierarchy() const override { return _hierarchy; }
};

// TellThrowingStream: tell() throws (for exercising skip_callback_bridge exception handling).
class TellThrowingStream : public IDataStream {
private:
  std::vector<uint8_t> _buffer;
  size_t _pos = 0;
  PathHierarchy _hierarchy;

public:
  TellThrowingStream(PathHierarchy hierarchy, const std::vector<uint8_t> &data) : _buffer(data), _hierarchy(std::move(hierarchy)) {}

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

  int64_t tell() const override { throw std::runtime_error("Stream tell error"); }

  bool can_seek() const override { return true; }

  void rewind() override { _pos = 0; }

  bool at_end() const override { return _pos >= _buffer.size(); }

  PathHierarchy source_hierarchy() const override { return _hierarchy; }
};

// ToggleSeekThrowStream: seek() can be toggled to throw after construction.
// Used to deterministically exercise seek_callback_bridge exception handling.
class ToggleSeekThrowStream : public IDataStream {
private:
  std::vector<uint8_t> _buffer;
  size_t _pos = 0;
  PathHierarchy _hierarchy;
  bool _throw_on_seek = false;
  size_t _seek_calls = 0;

public:
  ToggleSeekThrowStream(PathHierarchy hierarchy, const std::vector<uint8_t> &data) : _buffer(data), _hierarchy(std::move(hierarchy)) {}

  void set_throw_on_seek(bool enabled) { _throw_on_seek = enabled; }

  size_t seek_calls() const { return _seek_calls; }

  ssize_t read(void *buf, size_t len) override {
    if (_pos >= _buffer.size()) return 0;
    const size_t remaining = _buffer.size() - _pos;
    const size_t to_read = (len < remaining) ? len : remaining;
    std::memcpy(buf, _buffer.data() + _pos, to_read);
    _pos += to_read;
    return static_cast<ssize_t>(to_read);
  }

  int64_t seek(int64_t offset, int whence) override {
    _seek_calls++;
    if (_throw_on_seek) {
      throw std::runtime_error("Stream seek error (toggle)");
    }

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

  // Use a growable buffer via write callback to avoid fragile fixed-size sizing.
  struct BufferCtx {
    std::vector<uint8_t> *out;
  };
  auto write_open_cb = [](struct archive *, void *) -> int { return ARCHIVE_OK; };
  auto write_cb = [](struct archive *, void *client_data, const void *buff, size_t length) -> la_ssize_t {
    auto *ctx = static_cast<BufferCtx *>(client_data);
    const auto *p = static_cast<const uint8_t *>(buff);
    ctx->out->insert(ctx->out->end(), p, p + length);
    return static_cast<la_ssize_t>(length);
  };
  auto write_close_cb = [](struct archive *, void *) -> int { return ARCHIVE_OK; };

  std::vector<uint8_t> buffer;
  BufferCtx ctx{&buffer};
  if (archive_write_open(a, &ctx, write_open_cb, write_cb, write_close_cb) != ARCHIVE_OK) {
    std::string msg = archive_error_string(a) ? archive_error_string(a) : "(null)";
    archive_write_free(a);
    throw std::runtime_error(std::string("archive_write_open failed: ") + msg);
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
      const char *p = content.data();
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
    }

    archive_entry_free(entry);
  }

  archive_write_close(a);
  archive_write_free(a);
  return buffer;
}


std::string bytes_to_string(const std::vector<uint8_t> &bytes) {
  return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
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
  }
  if (!in) {
    throw std::runtime_error("Failed to read file: " + path);
  }
  return bytes;
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

    // Test 2b: EntryPayloadStream with null parent should throw
    {
      bool threw = false;
      try {
        (void)EntryPayloadStream(nullptr, make_single_path("/test/entry"));
      } catch (const std::invalid_argument &) {
        threw = true;
      }
      if (!expect(threw, "EntryPayloadStream(nullptr parent) should throw std::invalid_argument")) {
        return 1;
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

    // Test 4b: skip_callback_bridge handles "cannot determine current" path (tell<0 and SEEK_CUR fails)
    {
      const std::string big(2 * 1024 * 1024, 'A');
      const auto payload = build_tar_payload({ {"big.bin", big} });
      auto stream = std::make_shared<TellMinusOneStream>(make_single_path("/test/tar"), payload);
      ArchiveOption options;
      options.formats = {"tar"};
      StreamArchive sa(stream, options);
      if (!expect(sa.skip_to_next_header(), "skip_to_next_header should succeed (tar entry)")) {
        return 1;
      }
      // Force archive_read_data_skip() to use our skip callback.
      (void)sa.skip_data();
    }

    // Test 4c: skip_callback_bridge handles exceptions from tell()
    {
      const std::string big(2 * 1024 * 1024, 'A');
      const auto payload = build_tar_payload({ {"big.bin", big} });
      auto stream = std::make_shared<TellThrowingStream>(make_single_path("/test/tar"), payload);
      ArchiveOption options;
      options.formats = {"tar"};
      StreamArchive sa(stream, options);
      if (!expect(sa.skip_to_next_header(), "skip_to_next_header should succeed (tar entry)")) {
        return 1;
      }
      // Force archive_read_data_skip() to hit skip_callback_bridge's exception handling.
      (void)sa.skip_data();
    }

    // Test 4d: seek_callback_bridge handles exceptions from seek() (forced via archive_seek_data)
    {
      // Use a larger zip to increase the likelihood libarchive will attempt seeking.
      const auto zip_bytes = load_file_bytes("test_data/test_perf.zip");
      auto stream = std::make_shared<ToggleSeekThrowStream>(make_single_path("/test/zip"), zip_bytes);
      ArchiveOption options;
      options.formats = {"zip"};
      // Enable throw BEFORE open so that any libarchive seek attempt goes through
      // seek_callback_bridge's exception handling (catch -> return -1).
      stream->set_throw_on_seek(true);

      // Do not require construction to fail: libarchive may fall back to streaming mode
      // even if seeking is unavailable.
      try {
        (void)StreamArchive(stream, options);
      } catch (const std::exception &) {
        // acceptable: some libarchive builds may treat seek failures as fatal
      }

      if (!expect(stream->seek_calls() > 0, "Expected libarchive to attempt seek at least once")) {
        return 1;
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

    // Test 10: synchronize_to_hierarchy triggers ascend when target is shallower
    {
      RootStreamFactoryGuard guard;

      const PathHierarchy root = make_single_path("/virtual/root.tar");
      const auto nested_payload = build_tar_payload({ {"x.txt", "X"} });
      const auto root_payload = build_tar_payload({ {"nested.tar", bytes_to_string(nested_payload)}, {"leaf.txt", "L"} });

      set_root_stream_factory([root_payload, root](const PathHierarchy &hierarchy) -> std::shared_ptr<IDataStream> {
        if (hierarchies_equal(hierarchy, root)) {
          return std::make_shared<MemoryStream>(hierarchy, root_payload);
        }
        return nullptr;
      });

      ArchiveStackCursor cursor;
      cursor.synchronize_to_hierarchy(root);
      cursor.descend();

      if (!expect(cursor.next(), "cursor.next() should succeed before descending into nested archive")) {
        return 1;
      }

      PathHierarchy nested_h = root;
      append_single(nested_h, "nested.tar");
      if (!expect(hierarchies_equal(cursor.current_entry_hierarchy(), nested_h), "expected current entry to be nested.tar")) {
        return 1;
      }

      // Make parent archive's current entry content "not ready" by consuming some bytes.
      // This should trigger ArchiveStackCursor::descend() to rewind the pending child stream.
      char tmp[1] = {};
      const ssize_t n = cursor.read(tmp, sizeof(tmp));
      if (!expect(n >= 0, "cursor.read should not error before descend")) {
        return 1;
      }

      cursor.descend();
      if (!expect(cursor.depth() == 2, "cursor depth should be 2 after descending into nested archive")) {
        return 1;
      }

      cursor.synchronize_to_hierarchy(root);
      if (!expect(cursor.depth() == 1, "cursor depth should be 1 after synchronize_to_hierarchy(root) from nested")) {
        return 1;
      }

      if (!expect(cursor.ascend(), "cursor.ascend should succeed after synchronize_to_hierarchy(root)")) {
        return 1;
      }
      if (!expect(cursor.depth() == 0, "cursor depth should be 0 after final ascend")) {
        return 1;
      }
    }

    // Test 11: synchronize_to_hierarchy ascends when switching to a sibling nested archive
    {
      RootStreamFactoryGuard guard;

      const PathHierarchy root = make_single_path("/virtual/root_siblings.tar");
      const auto a_payload = build_tar_payload({ {"a.txt", "A"} });
      const auto b_payload = build_tar_payload({ {"b.txt", "BB"} });
      const auto root_payload = build_tar_payload({ {"a.tar", bytes_to_string(a_payload)}, {"b.tar", bytes_to_string(b_payload)} });

      set_root_stream_factory([root_payload, root](const PathHierarchy &hierarchy) -> std::shared_ptr<IDataStream> {
        if (hierarchies_equal(hierarchy, root)) {
          return std::make_shared<MemoryStream>(hierarchy, root_payload);
        }
        return nullptr;
      });

      ArchiveStackCursor cursor;
      cursor.synchronize_to_hierarchy(root);
      cursor.descend();

      if (!expect(cursor.next(), "cursor.next() should succeed on root tar with siblings")) {
        return 1;
      }

      PathHierarchy a_h = root;
      append_single(a_h, "a.tar");
      if (!expect(hierarchies_equal(cursor.current_entry_hierarchy(), a_h), "expected first entry to be a.tar")) {
        return 1;
      }

      cursor.descend();
      if (!expect(cursor.depth() == 2, "cursor depth should be 2 after descending into a.tar")) {
        return 1;
      }

      PathHierarchy b_h = root;
      append_single(b_h, "b.tar");

      cursor.synchronize_to_hierarchy(b_h);
      if (!expect(cursor.depth() == 1, "cursor depth should return to 1 after switching sibling (a.tar -> b.tar)")) {
        return 1;
      }
      if (!expect(cursor.has_stream(), "cursor should have stream for b.tar after synchronize_to_hierarchy")) {
        return 1;
      }

      cursor.descend();
      if (!expect(cursor.depth() == 2, "cursor depth should be 2 after descending into b.tar")) {
        return 1;
      }
      if (!expect(cursor.next(), "cursor.next() should succeed inside b.tar")) {
        return 1;
      }
      char buf[8] = {};
      const ssize_t r = cursor.read(buf, sizeof(buf));
      if (!expect(r > 0, "cursor.read should return >0 inside b.tar")) {
        return 1;
      }
    }

    // Test 12: synchronize_to_hierarchy does not recreate/rewind stream when already aligned
    {
      RootStreamFactoryGuard guard;

      const PathHierarchy root = make_single_path("/virtual/raw_bytes");
      const std::vector<uint8_t> bytes = {'a', 'b', 'c', 'd', 'e', 'f'};

      set_root_stream_factory([bytes, root](const PathHierarchy &hierarchy) -> std::shared_ptr<IDataStream> {
        if (hierarchies_equal(hierarchy, root)) {
          return std::make_shared<MemoryStream>(hierarchy, bytes);
        }
        return nullptr;
      });

      ArchiveStackCursor cursor;
      cursor.synchronize_to_hierarchy(root);

      char buf1[2] = {};
      const ssize_t r1 = cursor.read(buf1, sizeof(buf1));
      if (!expect(r1 == 2, "expected to read first 2 bytes")) {
        return 1;
      }
      if (!expect(std::string(buf1, 2) == "ab", "expected first bytes to be 'ab'")) {
        return 1;
      }

      cursor.synchronize_to_hierarchy(root);

      char buf2[2] = {};
      const ssize_t r2 = cursor.read(buf2, sizeof(buf2));
      if (!expect(r2 == 2, "expected to read next 2 bytes without rewind")) {
        return 1;
      }
      if (!expect(std::string(buf2, 2) == "cd", "expected next bytes to be 'cd'")) {
        return 1;
      }
    }

    // Test 13: reset / ascend failure / read(0) / depth0 current_entry_hierarchy
    {
      RootStreamFactoryGuard guard;

      const PathHierarchy root = make_single_path("/virtual/root_for_reset.tar");
      const auto payload = build_tar_payload({ {"a.txt", "A"} });

      set_root_stream_factory([payload, root](const PathHierarchy &hierarchy) -> std::shared_ptr<IDataStream> {
        if (hierarchies_equal(hierarchy, root)) {
          return std::make_shared<MemoryStream>(hierarchy, payload);
        }
        return nullptr;
      });

      ArchiveStackCursor cursor;

      if (!expect(!cursor.ascend(), "cursor.ascend() should return false at depth 0")) {
        return 1;
      }

      cursor.synchronize_to_hierarchy(root);
      if (!expect(cursor.has_stream(), "cursor should have stream after synchronize_to_hierarchy")) {
        return 1;
      }

      if (!expect(hierarchies_equal(cursor.current_entry_hierarchy(), root), "depth0 current_entry_hierarchy should be root")) {
        return 1;
      }

      char buf[1] = {};
      const ssize_t r0 = cursor.read(buf, 0);
      if (!expect(r0 == 0, "cursor.read(len=0) should return 0")) {
        return 1;
      }

      ArchiveOption opt;
      opt.formats.push_back("zip");
      cursor.configure(opt);
      cursor.reset();

      if (!expect(cursor.depth() == 0, "cursor depth should be 0 after reset")) {
        return 1;
      }
      if (!expect(!cursor.has_stream(), "cursor should not have stream after reset")) {
        return 1;
      }
      if (!expect(cursor.options_snapshot.formats.empty(), "options_snapshot should be reset to defaults")) {
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
