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

} // namespace

int main() {
  bool ok = true;

  std::vector<EntryFault> faults;
  register_fault_callback([&](const EntryFault &fault) { faults.push_back(fault); });
  CallbackReset reset;

  try {
    TraverserOptions opts;
    opts.formats = { "__archive_r_unsupported_format__" };

    Traverser t({ make_single_path("test_data/deeply_nested.tar.gz") }, opts);

    size_t entries = 0;
    for (Entry &e : t) {
      (void)e;
      ++entries;
    }

    ok = expect(entries == 1, "Expected traversal to stop at the root entry when an unsupported format is configured") && ok;
  } catch (const std::exception &ex) {
    std::cerr << "Unexpected exception: " << ex.what() << std::endl;
    ok = false;
  }

  ok = expect(!faults.empty(), "Expected a fault to be dispatched for unsupported format") && ok;
  if (!faults.empty()) {
    const std::string &msg = faults.front().message;
    ok = expect(msg.find("Unsupported archive format specified") != std::string::npos, "Fault message did not contain expected substring for unsupported format") && ok;
  }

  return ok ? 0 : 1;
}
