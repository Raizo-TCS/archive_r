// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/traverser.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
  const auto candidate = base / ("archive_r_test_traverser_" + std::to_string(static_cast<long long>(pid)));
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

  // End iterator dereference should throw.
  try {
    TraverserOptions opts;
    Traverser t(std::string("."), opts);
    auto e = t.end();
    (void)*e;
    std::cerr << "Expected exception when dereferencing end iterator" << std::endl;
    ok = false;
  } catch (const std::logic_error &) {
    // expected
  } catch (const std::exception &ex) {
    std::cerr << "Unexpected exception type: " << ex.what() << std::endl;
    ok = false;
  }

  // Moved-from iterators compare as expected.
  {
    TraverserOptions opts;
    Traverser t(std::string("."), opts);

    auto it0 = t.begin();
    auto it1 = std::move(it0);
    auto it2 = std::move(it1);

    ok = expect(it0 == it1, "Expected two moved-from iterators to be equal") && ok;
    ok = expect(it0 != it2, "Expected moved-from iterator to compare unequal with a valid iterator") && ok;
  }

  // Directory traversal should disable recursion when descent is disabled.
  {
    TempDir temp;
    const auto root_dir = temp.path;
    const auto child_dir = root_dir / "child";
    const auto root_file = root_dir / "root.txt";
    const auto child_file = child_dir / "nested.txt";

    std::error_code ec;
    std::filesystem::create_directories(child_dir, ec);
    if (ec) {
      std::cerr << "Failed to create directory: " << ec.message() << std::endl;
      return 1;
    }

    {
      std::ofstream out(root_file);
      out << "root";
    }
    {
      std::ofstream out(child_file);
      out << "nested";
    }

    TraverserOptions opts;
    opts.descend_archives = false;
    Traverser t(root_dir.string(), opts);

    std::vector<std::string> visited;
    for (auto it = t.begin(); it != t.end(); ++it) {
      visited.push_back(it->path());
    }

    const auto root_file_str = std::filesystem::path(root_file).lexically_normal().string();
    const auto child_file_str = std::filesystem::path(child_file).lexically_normal().string();

    bool saw_root_file = false;
    bool saw_child_file = false;
    for (const auto &p : visited) {
      if (p == root_file_str) {
        saw_root_file = true;
      }
      if (p == child_file_str) {
        saw_child_file = true;
      }
    }

    ok = expect(saw_root_file, "Expected to visit root file") && ok;
    ok = expect(!saw_child_file, "Expected NOT to visit nested file when recursion is disabled") && ok;
  }

  if (!ok) {
    return 1;
  }

  std::cout << "Traverser iterator semantics tests passed" << std::endl;
  return 0;
}
