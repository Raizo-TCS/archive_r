// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/traverser.h"

#include <iostream>
#include <string>
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

} // namespace

int main() {
  bool ok = true;

  // Empty vector should throw.
  try {
    TraverserOptions opts;
    Traverser t(std::vector<PathHierarchy>{}, opts);
    (void)t;
    std::cerr << "Expected exception for empty paths" << std::endl;
    ok = false;
  } catch (const std::invalid_argument &) {
    // expected
  } catch (const std::exception &ex) {
    std::cerr << "Unexpected exception type: " << ex.what() << std::endl;
    ok = false;
  }

  // Vector with an empty hierarchy should throw.
  try {
    TraverserOptions opts;
    std::vector<PathHierarchy> paths;
    paths.push_back(PathHierarchy{});
    Traverser t(std::move(paths), opts);
    (void)t;
    std::cerr << "Expected exception for empty hierarchy" << std::endl;
    ok = false;
  } catch (const std::invalid_argument &) {
    // expected
  } catch (const std::exception &ex) {
    std::cerr << "Unexpected exception type: " << ex.what() << std::endl;
    ok = false;
  }

  if (!expect(ok, "Traverser invalid-arg tests failed")) {
    return 1;
  }

  std::cout << "Traverser invalid-arg tests passed" << std::endl;
  return 0;
}
