// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_type.h"

#include <archive.h>
#include <iostream>

using namespace archive_r;

namespace {

struct DummyArchive final : Archive {
  void open_archive() override {}
};

bool expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << std::endl;
    return false;
  }
  return true;
}

} // namespace

int main() {
  bool ok = true;

  // Exercise archive_deleter null branch explicitly.
  {
    archive_deleter d;
    d(nullptr);
  }

  // Supported format list should exercise the configure_formats() loop path.
  // Use an existing tar.gz test fixture (format=tar, filter=gzip) to avoid custom payload generation.
  try {
    const std::string path = "test_data/deeply_nested.tar.gz";
    auto ar = new_read_archive_common({}, {"tar", "zip", "raw"}, [&](struct archive *a) {
      return archive_read_open_filename(a, path.c_str(), 10240);
    });
    (void)ar;
  } catch (const std::exception &ex) {
    ok = false;
    std::cerr << "Unexpected exception for supported format list: " << ex.what() << std::endl;
  }

  // Non-empty passphrases should execute set_passphrases() loop body (even if archive is not encrypted).
  try {
    const std::string path = "test_data/deeply_nested.tar.gz";
    auto ar = new_read_archive_common({"dummy-passphrase"}, {"tar"}, [&](struct archive *a) {
      return archive_read_open_filename(a, path.c_str(), 10240);
    });
    (void)ar;
  } catch (const std::exception &ex) {
    ok = false;
    std::cerr << "Unexpected exception for non-empty passphrases: " << ex.what() << std::endl;
  }

  // Unsupported format should raise an EntryFaultError.
  try {
    (void)new_read_archive_common({}, {"__archive_r_unsupported_format__"}, [](struct archive *ar) {
      (void)ar;
      return ARCHIVE_OK;
    });
    ok = false;
    std::cerr << "Expected exception for unsupported format" << std::endl;
  } catch (const EntryFaultError &) {
    // expected
  } catch (const std::exception &ex) {
    ok = false;
    std::cerr << "Unexpected exception type: " << ex.what() << std::endl;
  }

  // Some libarchive builds may not support certain formats (e.g., rar).
  // If archive_read_support_format_* returns non-OK, configure_formats() should throw.
  {
    bool threw = false;
    try {
      (void)new_read_archive_common({}, {"rar"}, [](struct archive *ar) {
        (void)ar;
        return ARCHIVE_OK;
      });
    } catch (const EntryFaultError &) {
      threw = true;
    } catch (const std::exception &ex) {
      threw = true;
      std::cerr << "Unexpected exception type for rar format: " << ex.what() << std::endl;
    }
    if (threw) {
      std::cout << "rar format support is not available; configure_formats failure path exercised" << std::endl;
    }
  }

  // skip_data() before reading any header should raise an EntryFaultError.
  try {
    const std::string path = "test_data/deeply_nested.tar.gz";
    auto ar = new_read_archive_common({}, {}, [&](struct archive *a) {
      return archive_read_open_filename(a, path.c_str(), 10240);
    });

    DummyArchive a;
    a._ar = ar.release();
    (void)a.skip_data();

    ok = false;
    std::cerr << "Expected exception for skip_data() before any header" << std::endl;
  } catch (const EntryFaultError &) {
    // expected
  } catch (const std::exception &ex) {
    ok = false;
    std::cerr << "Unexpected exception type: " << ex.what() << std::endl;
  }

  // Open failure path should raise an EntryFaultError with the formatted message.
  try {
    (void)new_read_archive_common({}, {}, [](struct archive *ar) {
      archive_set_error(ar, 123, "forced open failure");
      return ARCHIVE_FATAL;
    });
    ok = false;
    std::cerr << "Expected exception for open failure" << std::endl;
  } catch (const EntryFaultError &) {
    // expected
  } catch (const std::exception &ex) {
    ok = false;
    std::cerr << "Unexpected exception type: " << ex.what() << std::endl;
  }

  // Uninitialized archive handle should raise std::logic_error.
  try {
    DummyArchive a;
    (void)a.skip_to_next_header();
    ok = false;
    std::cerr << "Expected logic_error for skip_to_next_header without initialized handle" << std::endl;
  } catch (const std::logic_error &) {
    // expected
  } catch (const std::exception &ex) {
    ok = false;
    std::cerr << "Unexpected exception type: " << ex.what() << std::endl;
  }

  try {
    DummyArchive a;
    (void)a.skip_data();
    ok = false;
    std::cerr << "Expected logic_error for skip_data without initialized handle" << std::endl;
  } catch (const std::logic_error &) {
    // expected
  } catch (const std::exception &ex) {
    ok = false;
    std::cerr << "Unexpected exception type: " << ex.what() << std::endl;
  }

  try {
    DummyArchive a;
    uint8_t buf[16] = {};
    (void)a.read_current(buf, sizeof(buf));
    ok = false;
    std::cerr << "Expected logic_error for read_current without initialized handle" << std::endl;
  } catch (const std::logic_error &) {
    // expected
  } catch (const std::exception &ex) {
    ok = false;
    std::cerr << "Unexpected exception type: " << ex.what() << std::endl;
  }

  // No current_entry selected should return 0 for size/filetype.
  {
    DummyArchive a;
    if (!expect(a.current_entry_size() == 0, "Expected current_entry_size() == 0 when current_entry is null")) {
      ok = false;
    }
    if (!expect(a.current_entry_filetype() == 0, "Expected current_entry_filetype() == 0 when current_entry is null")) {
      ok = false;
    }
  }

  if (!expect(ok, "archive_type error path tests failed")) {
    return 1;
  }

  std::cout << "Archive type error paths exercised" << std::endl;
  return 0;
}
