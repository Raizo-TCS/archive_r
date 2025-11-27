// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"
#include <filesystem>
#include <iostream>
#include <string>

using namespace archive_r;

int main() {
  try {
    const std::string archive_path = std::filesystem::absolute("test_data/multi_volume_test.tar.gz").string();
    auto traverse_with_limit = [&](size_t registration_limit) {
      Traverser traverser({ make_single_path(archive_path) });
      size_t registered = 0;
      bool nested_seen = false;
      for (Entry &entry : traverser) {
        if (entry.depth() > 1) {
          nested_seen = true;
          break;
        }

        const std::string name = entry.name();
        if (name.rfind("archive.tar.gz.part", 0) == 0 && registered < registration_limit) {
          entry.set_multi_volume_group("archive.tar.gz");
          ++registered;
        }
      }
      return nested_seen;
    };

    if (traverse_with_limit(1)) {
      std::cerr << "Nested entries appeared despite incomplete multi-volume registration" << std::endl;
      return 1;
    }

    auto traverse_register_all = [&]() {
      Traverser traverser({ make_single_path(archive_path) });
      for (Entry &entry : traverser) {
        const std::string name = entry.name();
        if (name.rfind("archive.tar.gz.part", 0) == 0) {
          entry.set_multi_volume_group("archive.tar.gz");
        }
        if (entry.depth() > 1) {
          return true;
        }
      }
      return false;
    };

    if (!traverse_register_all()) {
      std::cerr << "Multi-volume retry did not activate even after all parts were registered" << std::endl;
      return 1;
    }

    std::cout << "\u2713 Test PASSED: Multi-volume retry requires explicit re-registration" << std::endl;
    return 0;
  } catch (const std::exception &ex) {
    std::cerr << "Exception: " << ex.what() << std::endl;
    return 1;
  }
}
