// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry_fault.h"
#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"
#include "archive_r/traverser.h"
#include "archive_stack_orchestrator.h"

#include <iostream>
#include <memory>
#include <string>

#if !defined(_WIN32)
#include <filesystem>
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

    // 4b) Entry copy with archived options (use_archive_metadata==true):
    //     - original is traverser-managed (has orchestrator)
    //     - copy has _orchestrator==nullptr but preserves _archive_options
    //     - copy.read() must exercise _archive_options.value_or(...) has-value path
    {
      Traverser traverser("test_data/deeply_nested.tar.gz");
      bool exercised = false;
      for (Entry &entry : traverser) {
        if (entry.depth() >= 1 && entry.is_file() && entry.path_hierarchy().size() > 1) {
          Entry copied(entry);
          char buf[16] = {};
          const ssize_t r = copied.read(buf, sizeof(buf));
          if (!expect(r >= 0, "copied entry read should succeed")) {
            return 1;
          }
          exercised = true;
          break;
        }
      }
      if (!expect(exercised, "expected to find at least one in-archive file entry")) {
        return 1;
      }
    }

    // 4c) Entry copy with no archived options: copy.read() exercises value_or(default) path.
    {
      auto original = Entry::create(make_single_path("test_data/no_uid.zip"), nullptr, true);
      Entry copied(*original);
      char buf[16] = {};
      const ssize_t r = copied.read(buf, sizeof(buf));
      if (!expect(r >= 0, "copied non-archive entry read should succeed")) {
        return 1;
      }
    }

    // 5) Test name() with empty hierarchy
    {
      auto e = Entry::create(PathHierarchy{}, nullptr, true);
      std::string n = e->name();
      if (!expect(n.empty(), "name should be empty for empty hierarchy")) {
        return 1;
      }
    }

    // 5b) Read error path: empty hierarchy should fail synchronize_to_hierarchy,
    //     making ensure_orchestrator() return nullptr and read() emit a fault.
    faults.reset();
    {
      auto e = Entry::create(PathHierarchy{}, nullptr, true);
      char buf[16] = {};
      const ssize_t r = e->read(buf, sizeof(buf));
      if (!expect(r == -1, "read should fail for empty hierarchy")) {
        return 1;
      }
      if (!expect(faults.count >= 1, "expected at least one fault on empty-hierarchy read")) {
        return 1;
      }
      if (!expect(faults.last_message.find("Failed to initialize ArchiveStackOrchestrator") != std::string::npos,
                  "expected read() to emit initialization fault")) {
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

    // 7) Exercise filetype fallback branch when filesystem type is unknown.
    // On POSIX, /dev/null is neither regular file nor directory nor symlink,
    // so collect_root_path_metadata() leaves filetype==0.
    // When an archive orchestrator exists at matching depth, Entry forces AE_IFREG.
#if !defined(_WIN32)
    {
      const std::string special = "/dev/null";
      if (std::filesystem::exists(special)) {
        ArchiveOption opts;
        auto orch = std::make_shared<ArchiveStackOrchestrator>(opts);
        PathHierarchy h = make_single_path(special);
        orch->open_root_hierarchy(h);

        auto e = Entry::create(h, orch, true);
        if (!expect(e->is_file(), "expected fallback to mark unknown root type as file")) {
          return 1;
        }
        if (!expect(!e->is_directory(), "fallback-marked entry should not be a directory")) {
          return 1;
        }
      }
    }

    // 8) Exercise the other short-circuit outcomes around the fallback condition.
    //    These cases are purely for coverage/branch completeness.
    {
      const std::string special = "/dev/null";
      if (std::filesystem::exists(special)) {
        // 8a) archive == nullptr path: orchestrator exists but has not opened any root.
        {
          ArchiveOption opts;
          auto orch = std::make_shared<ArchiveStackOrchestrator>(opts);
          // Use empty hierarchy to guarantee _filetype stays 0 (no filesystem metadata).
          auto e = Entry::create(PathHierarchy{}, orch, true);
          if (!expect(!e->is_file(), "expected not-a-file when archive is null and filetype is unknown")) {
            return 1;
          }
        }

        // 8b) depth mismatch path: archive exists, but hierarchy is empty so depth()!=size.
        {
          ArchiveOption opts;
          auto orch = std::make_shared<ArchiveStackOrchestrator>(opts);
          orch->open_root_hierarchy(make_single_path(special));

          auto e = Entry::create(PathHierarchy{}, orch, true);
          if (!expect(!e->is_file(), "expected not-a-file when hierarchy is empty (depth mismatch)")) {
            return 1;
          }
        }

        // 8c) _filetype != 0 path: regular file metadata sets filetype, so fallback should not apply.
        {
          ArchiveOption opts;
          auto orch = std::make_shared<ArchiveStackOrchestrator>(opts);
          const PathHierarchy h = make_single_path("test_data/no_uid.zip");
          orch->open_root_hierarchy(h);

          auto e = Entry::create(h, orch, true);
          if (!expect(e->is_file(), "expected regular file to be reported as file without fallback")) {
            return 1;
          }
        }
      }
    }
#endif

  } catch (const std::exception &ex) {
    std::cerr << "Entry impl copy/fault test failed: " << ex.what() << std::endl;
    return 1;
  }

  std::cout << "Entry impl copy and fault paths exercised" << std::endl;
  return 0;
}
