// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/traverser.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifndef _WIN32
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace archive_r;

namespace {

#ifndef _WIN32

struct ExpectedIdentity {
  uid_t uid;
  gid_t gid;
  std::optional<std::string> uname;
  std::optional<std::string> gname;
};

ExpectedIdentity get_expected_identity() {
  ExpectedIdentity info{};
  info.uid = ::getuid();
  info.gid = ::getgid();

  if (struct passwd *pwd = ::getpwuid(info.uid)) {
    if (pwd->pw_name) {
      info.uname = std::string(pwd->pw_name);
    }
  }
  if (struct group *grp = ::getgrgid(info.gid)) {
    if (grp->gr_name) {
      info.gname = std::string(grp->gr_name);
    }
  }
  return info;
}

bool validate_identity_metadata(Entry &entry, const ExpectedIdentity &expected) {
  const auto *uid_value = entry.find_metadata("uid");
  const auto *gid_value = entry.find_metadata("gid");
  const auto *uname_value = entry.find_metadata("uname");
  const auto *gname_value = entry.find_metadata("gname");

  if (!uid_value || !gid_value || !uname_value || !gname_value) {
    std::cerr << "Missing identity metadata for entry: " << entry.path() << std::endl;
    return false;
  }

  const int64_t *uid_int = std::get_if<int64_t>(uid_value);
  const int64_t *gid_int = std::get_if<int64_t>(gid_value);
  const std::string *uname_str = std::get_if<std::string>(uname_value);
  const std::string *gname_str = std::get_if<std::string>(gname_value);

  if (!uid_int || !gid_int || !uname_str || !gname_str) {
    std::cerr << "Identity metadata has unexpected types for entry: " << entry.path() << std::endl;
    return false;
  }

  if (static_cast<uid_t>(*uid_int) != expected.uid) {
    std::cerr << "UID metadata mismatch for entry: " << entry.path() << std::endl;
    return false;
  }

  if (static_cast<gid_t>(*gid_int) != expected.gid) {
    std::cerr << "GID metadata mismatch for entry: " << entry.path() << std::endl;
    return false;
  }

  if (expected.uname && *uname_str != *expected.uname) {
    std::cerr << "Uname metadata mismatch for entry: " << entry.path() << std::endl;
    return false;
  }

  if (expected.gname && *gname_str != *expected.gname) {
    std::cerr << "Gname metadata mismatch for entry: " << entry.path() << std::endl;
    return false;
  }

  return true;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <directory>" << std::endl;
    return 1;
  }

  const std::string directory_path = argv[1];

  TraverserOptions options;
  options.metadata_keys = { "uid", "gid", "uname", "gname", "pathname" };

  const ExpectedIdentity expected_identity = get_expected_identity();
  bool validated = false;

  try {
    Traverser traverser({ make_single_path(directory_path) }, options);
    for (auto it = traverser.begin(); it != traverser.end(); ++it) {
      Entry &entry = *it;
      if (entry.depth() != 0 || !entry.is_file()) {
        continue; // Only consider top-level filesystem files.
      }

      if (!validate_identity_metadata(entry, expected_identity)) {
        return 1;
      }

      validated = true;
      break;
    }
  } catch (const std::exception &ex) {
    std::cerr << "Traversal failed: " << ex.what() << std::endl;
    return 1;
  }

  if (!validated) {
    std::cerr << "No top-level file entries were validated in directory: " << directory_path << std::endl;
    return 1;
  }

  std::cout << "Filesystem metadata identity verification succeeded" << std::endl;
  return 0;
}

#else // _WIN32

} // namespace

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;
  std::cout << "Filesystem metadata identity verification skipped on Windows" << std::endl;
  return 0;
}

#endif // _WIN32
