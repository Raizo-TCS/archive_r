// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/traverser.h"
#include "archive_r/path_hierarchy_utils.h"
#include <iostream>
#include <string>
#include <variant>
#include <vector>

using namespace archive_r;

namespace {

bool validate_basic_metadata(Entry &entry) {
  const auto *pathname_value = entry.find_metadata("pathname");
  if (!pathname_value) {
    std::cerr << "Missing pathname metadata" << std::endl;
    return false;
  }
  const std::string *pathname = std::get_if<std::string>(pathname_value);
  if (!pathname) {
    std::cerr << "Pathname metadata has unexpected type" << std::endl;
    return false;
  }

  const auto &hierarchy = entry.path_hierarchy();
  const std::string expected_pathname = hierarchy.empty() ? std::string() : path_entry_display(hierarchy.back());
  if (*pathname != expected_pathname) {
    std::cerr << "Pathname metadata value mismatch: expected '" << expected_pathname << "' but got '" << *pathname << "'" << std::endl;
    return false;
  }

  const auto *size_value = entry.find_metadata("size");
  if (!size_value) {
    std::cerr << "Missing size metadata" << std::endl;
    return false;
  }
  const uint64_t *size = std::get_if<uint64_t>(size_value);
  if (!size) {
    std::cerr << "Size metadata has unexpected type" << std::endl;
    return false;
  }
  if (*size != entry.size()) {
    std::cerr << "Size metadata value mismatch: expected " << entry.size() << " but got " << *size << std::endl;
    return false;
  }

  return true;
}

bool validate_missing_uid(Entry &entry) {
  if (entry.find_metadata("uid") != nullptr) {
    std::cerr << "Unexpected uid metadata captured" << std::endl;
    return false;
  }

  const auto &map = entry.metadata();
  if (map.find("uid") != map.end()) {
    std::cerr << "uid key unexpectedly present in metadata map" << std::endl;
    return false;
  }
  return true;
}

bool check_archive(const std::string &path, const std::vector<std::string> &metadata_keys, bool expect_uid_missing) {
  TraverserOptions options;
  options.metadata_keys = metadata_keys;

  Traverser traverser({ make_single_path(path) }, options);
  auto it = traverser.begin();
  if (it == traverser.end()) {
    std::cerr << "Archive produced no entries: " << path << std::endl;
    return false;
  }

  Entry *entry_ptr = &(*it);

  if (entry_ptr->depth() == 0) {
    ++it;
    if (it == traverser.end()) {
      std::cerr << "Archive traversal ended before reaching nested entries: " << path << std::endl;
      return false;
    }
    entry_ptr = &(*it);
  }

  Entry &entry = *entry_ptr;
  if (!validate_basic_metadata(entry)) {
    return false;
  }

  if (expect_uid_missing) {
    if (!validate_missing_uid(entry)) {
      return false;
    }
  }

  std::cout << "Validated metadata for: " << entry.path() << std::endl;
  return true;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: " << argv[0] << " <archive_with_metadata> <archive_missing_uid>" << std::endl;
    return 1;
  }

  const std::string metadata_archive = argv[1];
  const std::string missing_uid_archive = argv[2];

  try {
    if (!check_archive(metadata_archive, { "pathname", "size" }, false)) {
      return 1;
    }
    if (!check_archive(missing_uid_archive, { "pathname", "size", "uid" }, true)) {
      return 1;
    }
  } catch (const std::exception &ex) {
    std::cerr << "Traversal failed: " << ex.what() << std::endl;
    return 1;
  }

  std::cout << "Metadata option verification succeeded" << std::endl;
  return 0;
}
