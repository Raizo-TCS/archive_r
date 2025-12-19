// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry.h"
#include "archive_r/entry_fault.h"
#include "archive_r/path_hierarchy.h"

#include <iostream>
#include <string>
#include <vector>

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
  std::vector<EntryFault> faults;

  register_fault_callback([&faults](const EntryFault &fault) { faults.push_back(fault); });

  PathHierarchy hierarchy;
  hierarchy.emplace_back(PathEntry::single("this_file_should_not_exist_12345.zip"));

  auto entry = Entry::create(hierarchy, nullptr, true);

  faults.clear();
  entry->set_descent(false);
  ok = expect(!faults.empty(), "Expected fault for set_descent on non-traverser Entry") && ok;
  if (!faults.empty()) {
    ok = expect(faults.back().message.find("set_descent") != std::string::npos, "Expected set_descent fault message") && ok;
  }

  faults.clear();
  entry->set_multi_volume_group("base_name");
  ok = expect(!faults.empty(), "Expected fault for set_multi_volume_group on non-traverser Entry") && ok;
  if (!faults.empty()) {
    ok = expect(faults.back().message.find("set_multi_volume_group") != std::string::npos, "Expected set_multi_volume_group fault message") && ok;
  }

  faults.clear();
  char buffer[16];
  const ssize_t rc = entry->read(buffer, sizeof(buffer));
  ok = expect(rc == -1, "Expected read() to fail for non-existent hierarchy") && ok;
  ok = expect(!faults.empty(), "Expected fault to be dispatched on read() failure") && ok;
  if (!faults.empty()) {
    ok = expect(!faults.back().message.empty(), "Expected read() fault message to be non-empty") && ok;
  }

  register_fault_callback(FaultCallback{});

  if (!ok) {
    return 1;
  }

  std::cout << "Entry fault path tests passed" << std::endl;
  return 0;
}
