// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/traverser.h"
#include <iostream>
#include <string>

using namespace archive_r;

bool expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << std::endl;
    return false;
  }
  return true;
}

int main() {
  // Test simple_traverse with a directory
  // Use the test directory or something that exists
  std::string test_path = ".";  // current directory

  try {
    Traverser traverser({ make_single_path(test_path) });

    size_t count = 0;
    for (auto &entry : traverser) {
      // Just iterate to exercise the code
      count++;
      if (count > 10) {  // limit to avoid too much output
        break;
      }
    }

    if (!expect(count > 0, "should find at least one entry")) {
      return 1;
    }

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Find and traverse exercised" << std::endl;
  return 0;
}