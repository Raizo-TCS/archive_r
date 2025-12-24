// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry_fault.h"
#include "archive_r/traverser.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#else
#include <process.h>
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
  const auto candidate = base / ("archive_r_test_pending_mv_fault_" + std::to_string(static_cast<long long>(pid)));
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
  TempDir()
      : path(unique_temp_dir()) {}
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
};

} // namespace

int main() {
  bool ok = true;

#if defined(_WIN32)
  // Windows ACL handling differs; keep this test POSIX-only for determinism.
  std::cout << "[SKIP] pending multi-volume fault test (Windows)" << std::endl;
  return 0;
#else
  TempDir temp;
  const auto root_dir = temp.path;

  const auto part1 = root_dir / "broken_archive.tar.gz.part001";
  const auto part2 = root_dir / "broken_archive.tar.gz.part002";

  {
    std::ofstream out(part1, std::ios::binary);
    out << "part1";
  }
  {
    std::ofstream out(part2, std::ios::binary);
    out << "part2";
  }

  // Make one part unreadable so multi-volume stream open fails.
  if (::chmod(part2.string().c_str(), 0) != 0) {
    std::cerr << "chmod failed" << std::endl;
    return 1;
  }

  bool saw_fault = false;
  register_fault_callback([&](const EntryFault &fault) {
    (void)fault;
    saw_fault = true;
  });

  try {
    TraverserOptions opts;
    opts.descend_archives = true;
    Traverser t(root_dir.string(), opts);

    for (Entry &e : t) {
      const std::string p = e.path();
      if (p.find("broken_archive.tar.gz.part") != std::string::npos) {
        e.set_multi_volume_group("broken_archive.tar.gz");
      }
    }
  } catch (const std::exception &ex) {
    std::cerr << "Unexpected exception: " << ex.what() << std::endl;
    ok = false;
  }

  register_fault_callback({});

  ok = expect(saw_fault, "Expected a fault to be dispatched when descending pending multi-volume groups") && ok;

  return ok ? 0 : 1;
#endif
}
