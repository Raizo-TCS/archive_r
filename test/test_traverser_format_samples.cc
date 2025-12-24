// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry_fault.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"

#include <iostream>
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

struct CallbackReset {
  ~CallbackReset() { register_fault_callback({}); }
};

bool run_format_case(const std::string &path, const std::string &format_name) {
  bool ok = true;

  std::vector<EntryFault> faults;
  register_fault_callback([&](const EntryFault &fault) { faults.push_back(fault); });
  CallbackReset reset;

  size_t entries = 0;

  try {
    TraverserOptions opts;
    // Use a broader allow-list to match existing coverage tests (and to avoid
    // environment-specific behavior where a single-format allow-list may fail
    // to recognize certain archives even when libarchive can parse them).
    opts.formats = { "7zip", "ar", "cab", "cpio", "empty", "iso9660", "lha", "mtree", "rar", "tar", "warc", "xar", "zip" };

    Traverser t({ make_single_path(path) }, opts);

    for (Entry &e : t) {
      // Prevent probing non-archive payload entries as nested archives.
      // (We only want to validate that the root archive opens with the given formats.)
      if (e.depth() >= 1) {
        e.set_descent(false);
      }
      (void)e;
      ++entries;
    }

    if (format_name == "ar" && faults.size() == 1 && faults[0].message.find("Unrecognized archive format") != std::string::npos) {
      // Some Windows libarchive builds may not include ar support.
      std::cout << "[SKIP] ar format not supported by libarchive in this environment: " << faults[0].message << std::endl;
      return true;
    }

    ok = expect(faults.empty(), "Expected no faults for format='" + format_name + "' path='" + path + "'") && ok;
    ok = expect(entries >= 2, "Expected at least 2 entries (root + >=1 inner) for format='" + format_name + "' path='" + path + "'") && ok;
  } catch (const std::exception &ex) {
    std::cerr << "Unexpected exception for format='" << format_name << "': " << ex.what() << std::endl;
    ok = false;
  }

  if (!ok) {
    std::cerr << "DEBUG: traverser_format_samples failure details" << std::endl;
    std::cerr << "  format='" << format_name << "'" << std::endl;
    std::cerr << "  path='" << path << "'" << std::endl;
    std::cerr << "  entries=" << entries << std::endl;
    std::cerr << "  faults.count=" << faults.size() << std::endl;
    for (size_t i = 0; i < faults.size(); ++i) {
      const auto &f = faults[i];
      std::cerr << "  fault[" << i << "]: message='" << f.message << "' errno=" << f.errno_value << " path='" << hierarchy_display(f.hierarchy) << "'" << std::endl;
    }
  }

  return ok;
}

} // namespace

int main() {
  bool ok = true;

  ok = run_format_case("test_data/format_ar_test.ar", "ar") && ok;
  ok = run_format_case("test_data/format_cpio_test.cpio", "cpio") && ok;

  return ok ? 0 : 1;
}
