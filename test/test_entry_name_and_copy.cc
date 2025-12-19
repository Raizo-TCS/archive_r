// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"

#include <iostream>
#include <string>

using namespace archive_r;

namespace {

bool expect(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << std::endl;
    return false;
  }
  return true;
}

} // namespace

int main() {
  bool ok = true;

  {
    auto e = Entry::create(PathHierarchy{}, nullptr, true);
    ok = expect(e->name().empty(), "Expected empty name for empty hierarchy") && ok;
  }

  {
    PathHierarchy h;
    h.emplace_back(PathEntry::single(""));
    auto e = Entry::create(h, nullptr, true);
    ok = expect(e->name().empty(), "Expected empty name for empty single entry") && ok;
  }

  {
    PathHierarchy h;
    h.emplace_back(PathEntry::multi_volume({"", "x"}));
    auto e = Entry::create(h, nullptr, true);
    ok = expect(e->name() == "[|x]", "Expected multi-volume display when first part is empty") && ok;
  }

  {
    PathHierarchy h;
    h.emplace_back(PathEntry::multi_volume({"part1", "part2"}));
    auto e = Entry::create(h, nullptr, true);
    ok = expect(e->name() == "part1", "Expected multi-volume name to be first part") && ok;
  }

  {
    PathHierarchy h;
    h.emplace_back(PathEntry::single("alpha"));
    auto e = Entry::create(h, nullptr, true);

    Entry copy_constructed(*e);
    ok = expect(copy_constructed.name() == e->name(), "Expected copy-constructed Entry to preserve name") && ok;

    Entry copy_assigned = copy_constructed;
    ok = expect(copy_assigned.name() == e->name(), "Expected copy-assigned Entry to preserve name") && ok;

    copy_assigned = copy_assigned;
    ok = expect(copy_assigned.name() == e->name(), "Expected self-assignment to keep name") && ok;
  }

  if (!ok) {
    return 1;
  }

  std::cout << "Entry name/copy tests passed" << std::endl;
  return 0;
}
