// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_type.h"

#include <archive.h>
#include <iostream>

using namespace archive_r;

namespace {

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

  if (!expect(ok, "archive_type error path tests failed")) {
    return 1;
  }

  std::cout << "Archive type error paths exercised" << std::endl;
  return 0;
}
