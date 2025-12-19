// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"

#include "archive_stack_orchestrator.h"
#include "entry_fault_error.h"
#include "multi_volume_manager.h"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#if !defined(_WIN32)
#  include <unistd.h>
#else
#  include <process.h>
#endif

using namespace archive_r;

namespace {

bool expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << std::endl;
    return false;
  }
  return true;
}

std::filesystem::path unique_temp_dir() {
  const auto base = std::filesystem::temp_directory_path();
  const auto pid =
#if defined(_WIN32)
      _getpid();
#else
      getpid();
#endif
  const auto candidate = base / ("archive_r_test_errutil_" + std::to_string(static_cast<long long>(pid)));
  std::error_code ec;
  std::filesystem::remove_all(candidate, ec);
  std::filesystem::create_directories(candidate, ec);
  if (ec) {
    throw std::runtime_error("Failed to create temp directory: " + ec.message());
  }
  return candidate;
}

struct TempDir {
  std::filesystem::path path;
  TempDir() : path(unique_temp_dir()) {}
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

} // namespace

int main() {
  bool ok = true;

  // ---- path_hierarchy.h header branches ----
  {
    bool threw = false;
    try {
      (void)PathEntry::multi_volume({});
    } catch (const std::invalid_argument &) {
      threw = true;
    }
    ok = expect(threw, "Expected PathEntry::multi_volume({}) to throw invalid_argument") && ok;
  }

  // ---- entry_fault_error.cc utilities ----
  {
    EntryFault fault;
    fault.message = "fault message";

    EntryFaultError direct(std::move(fault));
    ok = expect(std::string(direct.what()) == "fault message", "Expected EntryFaultError::what() to use fault message") && ok;
  }

  // ---- multi_volume_manager.cc early-return guard ----
  {
    MultiVolumeManager manager;
    PathHierarchy empty_entry;
    manager.mark_entry_as_multi_volume(empty_entry, "base", PathEntry::Parts::Ordering::Given);

    PathHierarchy out;
    ok = expect(!manager.pop_multi_volume_group(PathHierarchy{}, out), "Expected no group after marking empty entry_path") && ok;
  }

  // ---- path_hierarchy.cc compare_entries multi-volume ordering ----
  {
    PathEntry natural = PathEntry::multi_volume({"a.part001"}, PathEntry::Parts::Ordering::Natural);
    PathEntry given = PathEntry::multi_volume({"a.part001"}, PathEntry::Parts::Ordering::Given);
    ok = expect(compare_entries(natural, given) != 0, "Expected compare_entries() to detect ordering difference") && ok;
  }

  {
    EntryFault fault;
    fault.message = "fault message";

    EntryFaultError internal(std::move(fault), "internal message");
    ok = expect(std::string(internal.what()) == "internal message", "Expected internal message to override fault message") && ok;
  }

  {
    EntryFaultError err = make_entry_fault_error("hello", PathHierarchy{}, ENOENT);
    const std::string msg = err.what();
    ok = expect(msg.find("hello") != std::string::npos, "Expected make_entry_fault_error to preserve message") && ok;
  }

  {
    ok = expect(format_errno_error("prefix", 0) == "prefix", "Expected format_errno_error(prefix,0) == prefix") && ok;

    const std::string msg = format_errno_error("prefix", ENOENT);
    ok = expect(msg.find("prefix") == 0, "Expected format_errno_error to start with prefix") && ok;
    ok = expect(msg.find("posix errno=") != std::string::npos, "Expected format_errno_error to include errno detail") && ok;
  }

  {
    ok = expect(format_path_errno_error("open", "", 0) == "open", "Expected empty path + err=0 to return action only") && ok;

    const std::string msg = format_path_errno_error("open", "some/path", ENOENT);
    ok = expect(msg.find("open '") == 0, "Expected format_path_errno_error to include action and quoted path") && ok;
    ok = expect(msg.find("posix errno=") != std::string::npos, "Expected format_path_errno_error to include errno detail") && ok;
  }

  {
    ok = expect(prefer_error_detail("", "fallback") == "fallback", "Expected prefer_error_detail to use fallback when empty") && ok;
    ok = expect(prefer_error_detail("detail", "fallback") == "detail", "Expected prefer_error_detail to use detail when non-empty") && ok;
  }

  // ---- archive_stack_orchestrator.cc error/empty paths ----
  {
    ArchiveStackOrchestrator orch;
    ok = expect(orch.depth() == 0, "Expected fresh ArchiveStackOrchestrator depth() == 0") && ok;
    ok = expect(orch.current_entryname().empty(), "Expected current_entryname() to be empty at depth 0") && ok;

    ok = expect(!orch.synchronize_to_hierarchy(PathHierarchy{}), "Expected synchronize_to_hierarchy(empty) to fail") && ok;

    char buf[8] = {0};
    const ssize_t n = orch.read_head(buf, sizeof(buf));
    ok = expect(n <= 0, "Expected read_head() to return <=0 before open_root_hierarchy") && ok;
  }

  // Exercise ArchiveStackOrchestrator::current_entryname() with a live archive.
  {
    ArchiveStackOrchestrator orch;
    const std::filesystem::path archive_path = std::filesystem::path("test_data") / "deeply_nested.tar.gz";
    ok = expect(std::filesystem::exists(archive_path), "Expected test archive to exist: test_data/deeply_nested.tar.gz") && ok;

    PathHierarchy root;
    root.emplace_back(PathEntry::single(archive_path.string()));
    orch.open_root_hierarchy(root);
    ok = expect(orch.depth() > 0, "Expected depth() > 0 after open_root_hierarchy") && ok;

    (void)orch.current_entryname();
  }

  // ---- entry.cc ensure_orchestrator reset path (traverser-managed depth-0 orchestrator) ----
  {
    TempDir temp;
    const auto file_path = temp.path / "file.txt";
    const std::string content = "abc";
    {
      std::ofstream out(file_path);
      out << content;
    }

    PathHierarchy h;
    h.emplace_back(PathEntry::single(file_path.string()));

    auto shared_orch = std::make_shared<ArchiveStackOrchestrator>();
    auto e = Entry::create(h, shared_orch, true);

    char buffer[16] = {0};
    const ssize_t n = e->read(buffer, sizeof(buffer));
    ok = expect(n == static_cast<ssize_t>(content.size()), "Expected Entry::read() to read full filesystem file") && ok;

    const std::string read_back(buffer, buffer + std::max<ssize_t>(0, n));
    ok = expect(read_back == content, "Expected Entry::read() content to match filesystem file") && ok;

    // Exercise metadata accessors even when metadata keys are empty.
    (void)e->metadata();
    ok = expect(e->find_metadata("pathname") == nullptr, "Expected find_metadata() to return nullptr when metadata not collected") && ok;
  }

  // ---- Entry copy-assignment (Entry::operator=) ----
  {
    PathHierarchy h1;
    h1.emplace_back(PathEntry::single("alpha"));
    Entry a(*Entry::create(h1, nullptr, true));

    PathHierarchy h2;
    h2.emplace_back(PathEntry::single("beta"));
    Entry b(*Entry::create(h2, nullptr, true));

    a = b;
    ok = expect(a.name() == b.name(), "Expected Entry copy assignment to copy name") && ok;
  }

  if (!ok) {
    return 1;
  }

  std::cout << "Error utilities / orchestrator path tests passed" << std::endl;
  return 0;
}
