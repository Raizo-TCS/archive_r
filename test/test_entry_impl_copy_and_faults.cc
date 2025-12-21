// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry_fault.h"
#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"
#include "archive_stack_orchestrator.h"

#include <iostream>
#include <memory>
#include <string>

using namespace archive_r;

namespace {

bool expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << std::endl;
    return false;
  }
  return true;
}

struct FaultCapture {
  int count = 0;
  std::string last_message;

  void reset() {
    count = 0;
    last_message.clear();
  }
};

} // namespace

int main() {
  FaultCapture faults;
  register_fault_callback([&faults](const EntryFault &fault) {
    ++faults.count;
    faults.last_message = fault.message;
  });

  try {
    // 1) Fault paths: calling control methods on non-traverser-managed Entry should fault.
    faults.reset();
    {
      auto e = Entry::create(make_single_path("test_data/no_uid.zip"), nullptr, true);
      if (!expect(!e->name().empty(), "name should not be empty for existing file path")) {
        return 1;
      }
      e->set_descent(false);
      e->set_multi_volume_group("archive.tar.gz");
      if (!expect(faults.count >= 2, "expected faults for non-traverser-managed controls")) {
        return 1;
      }
    }

    // 2) Read error path: synchronize failure should emit fault and return -1.
    faults.reset();
    {
      auto e = Entry::create(make_single_path("test_data/does_not_exist_999999.zip"), nullptr, true);
      char buf[16] = {};
      const ssize_t r = e->read(buf, sizeof(buf));
      if (!expect(r == -1, "read should fail for nonexistent path")) {
        return 1;
      }
      if (!expect(faults.count >= 1, "expected at least one fault on read failure")) {
        return 1;
      }
    }

    // 3) Read success path + orchestrator detachment branch.
    // Pass a traverser-managed orchestrator that has depth==0 to trigger detachment.
    faults.reset();
    {
      ArchiveOption opts;
      auto traverser_orch = std::make_shared<ArchiveStackOrchestrator>(opts);
      auto e = Entry::create(make_single_path("test_data/no_uid.zip"), traverser_orch, true);

      char buf[16] = {};
      const ssize_t r = e->read(buf, sizeof(buf));
      if (!expect(r >= 0, "read should succeed for existing path")) {
        return 1;
      }
      if (!expect(!e->descent_enabled(), "descent should be disabled after read")) {
        return 1;
      }
    }

    // 4) Entry copy operations exercise Entry::{copy ctor, copy assignment}.
    {
      auto original = Entry::create(make_single_path("test_data/no_uid.zip"), nullptr, true);
      Entry copy(*original);
      Entry assigned = *original;
      assigned = *original;
      (void)copy;
    }

    // 5) Test name() with empty hierarchy
    {
      auto e = Entry::create(PathHierarchy{}, nullptr, true);
      std::string n = e->name();
      if (!expect(n.empty(), "name should be empty for empty hierarchy")) {
        return 1;
      }
    }

    // 6) Test read() error paths
    {
      auto e = Entry::create(make_single_path("nonexistent_file"), nullptr, true);
      char buf[16] = {};
      const ssize_t r = e->read(buf, sizeof(buf));
      if (!expect(r < 0, "read should fail for nonexistent file")) {
        return 1;
      }
      if (!expect(faults.count > 0, "read failure should emit fault")) {
        return 1;
      }
    }

  } catch (const std::exception &ex) {
    std::cerr << "Entry impl copy/fault test failed: " << ex.what() << std::endl;
    return 1;
  }

  std::cout << "Entry impl copy and fault paths exercised" << std::endl;
  return 0;
}
