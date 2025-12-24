// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/data_stream.h"
#include "archive_r/entry.h"
#include "archive_r/entry_fault.h"
#include "archive_r/path_hierarchy.h"
#include "archive_r/path_hierarchy_utils.h"
#include "archive_r/traverser.h"
#include "archive_stack_orchestrator.h"
#include "entry_fault_error.h"
#include "system_file_stream.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace archive_r;

namespace {

class ImmediateFaultStream : public IDataStream {
public:
  explicit ImmediateFaultStream(PathHierarchy hierarchy)
      : _hierarchy(std::move(hierarchy)) {
    active_instances().fetch_add(1);
  }

  ~ImmediateFaultStream() override { active_instances().fetch_sub(1); }

  ssize_t read(void * /*buffer*/, size_t /*size*/) override { return -1; }

  void rewind() override {}

  bool at_end() const override { return true; }

  PathHierarchy source_hierarchy() const override { return _hierarchy; }

  static void reset_instance_tracking() { active_instances().store(0); }
  static int active_instance_count() { return active_instances().load(); }

private:
  PathHierarchy _hierarchy;

  static std::atomic<int> &active_instances() {
    static std::atomic<int> counter{ 0 };
    return counter;
  }
};

void report_result(const std::string &name, bool success) { std::cout << "[" << (success ? "PASS" : "FAIL") << "] " << name << std::endl; }

bool test_broken_nested_archive() {
  const std::string archive_path = "test_data/broken_nested.tar";
  Traverser traverser({ make_single_path(archive_path) });

  bool saw_corrupt_entry = false;
  bool saw_ok_file = false;

  for (Entry &entry : traverser) {
    const std::string entry_name = entry.name();
    if (entry_name == "corrupt_inner.tar") {
      saw_corrupt_entry = true;
    }
    if (entry_name == "ok.txt") {
      saw_ok_file = true;
    }
  }

  if (!saw_corrupt_entry) {
    std::cerr << "broken_nested: corrupt_inner.tar was not enumerated" << std::endl;
    return false;
  }

  if (!saw_ok_file) {
    std::cerr << "broken_nested: ok.txt was not enumerated (traverser should continue after corrupt entry)" << std::endl;
    return false;
  }

  return true;
}

bool test_missing_multi_volume_part() {
  PathHierarchy multi_root;
  append_multi_volume(multi_root, { "test_data/multi_volume_missing_part.tar.part001", "test_data/multi_volume_missing_part.tar.part002", "test_data/multi_volume_missing_part.tar.part003" });

  try {
    Traverser traverser({ multi_root });
    size_t enumerated_entries = 0;
    for (Entry &entry : traverser) {
      (void)entry;
      ++enumerated_entries;
    }

    if (enumerated_entries == 0) {
      std::cerr << "missing_multi_volume: expected traversal to enumerate available parts even when some are missing" << std::endl;
      return false;
    }

    std::cout << "missing_multi_volume: enumerated " << enumerated_entries << " entries despite missing parts (expected behavior)" << std::endl;
    return true;
  } catch (const std::exception &ex) {
    std::cerr << "missing_multi_volume: traversal raised unexpected exception: " << ex.what() << std::endl;
    return false;
  }
}

bool test_injected_stream_error() {
  PathHierarchy hierarchy = make_single_path("test_data/deeply_nested.tar.gz");
  auto stream = std::make_shared<ImmediateFaultStream>(hierarchy);

  try {
    StreamArchive archive(stream);
    (void)archive; // constructor should throw before this line
  } catch (const EntryFaultError &error) {
    std::cout << "injected_stream_error: EntryFaultError message= " << error.what() << std::endl;
    return true;
  } catch (const std::exception &ex) {
    std::cerr << "injected_stream_error: unexpected exception type: " << ex.what() << std::endl;
    return false;
  }

  std::cerr << "injected_stream_error: expected EntryFaultError but constructor succeeded" << std::endl;
  return false;
}

bool test_stream_archive_open_failure_releases_stream() {
  const PathHierarchy hierarchy = make_single_path("virtual://immediate_fault");
  bool saw_entry_fault = false;
  ImmediateFaultStream::reset_instance_tracking();

  {
    auto stream = std::make_shared<ImmediateFaultStream>(hierarchy);
    try {
      StreamArchive archive(stream);
      std::cerr << "stream_archive_open_failure: constructor unexpectedly succeeded" << std::endl;
      return false;
    } catch (const EntryFaultError &error) {
      (void)error;
      saw_entry_fault = true;
    }
  }

  if (!saw_entry_fault) {
    std::cerr << "stream_archive_open_failure: EntryFaultError was not raised" << std::endl;
    return false;
  }

  if (ImmediateFaultStream::active_instance_count() != 0) {
    std::cerr << "stream_archive_open_failure: ImmediateFaultStream instances still active" << std::endl;
    return false;
  }

  return true;
}

bool test_archive_entry_stream_missing_part_fault() {
  PathHierarchy root = make_single_path("test_data/deeply_nested.tar.gz");
  auto root_stream = std::make_shared<SystemFileStream>(root);
  auto archive = std::make_shared<StreamArchive>(root_stream);

  PathHierarchy logical = root;
  append_multi_volume(logical, { "root.txt", "root.txt_missing_part" });

  EntryPayloadStream entry_stream(archive, logical);
  std::vector<char> buffer(4096);

  try {
    while (entry_stream.read(buffer.data(), buffer.size()) > 0) {
      // consume entire first part
    }
  } catch (const EntryFaultError &error) {
    const std::string message = error.what();
    if (message.find("does not contain requested stream part") == std::string::npos) {
      std::cerr << "archive_entry_stream_missing_part: unexpected error message: " << message << std::endl;
      return false;
    }
    std::cout << "archive_entry_stream_missing_part: captured fault message: " << message << std::endl;
    return true;
  }

  std::cerr << "archive_entry_stream_missing_part: expected EntryFaultError did not occur" << std::endl;
  return false;
}

bool test_nested_fault_callback_propagation() {
  std::vector<EntryFault> captured_faults;
  register_fault_callback([&](const EntryFault &fault) { captured_faults.push_back(fault); });
  struct CallbackReset {
    ~CallbackReset() { register_fault_callback({}); }
  } reset_callback; // Ensure global callback does not leak into other tests

  Traverser traverser({ make_single_path("test_data/broken_nested.tar") });
  bool saw_ok_file = false;
  for (Entry &entry : traverser) {
    if (entry.name() == "ok.txt") {
      saw_ok_file = true;
    }
  }

  if (!saw_ok_file) {
    std::cerr << "nested_fault_callback: ok.txt was not reached" << std::endl;
    return false;
  }

  const bool saw_corrupt = std::any_of(captured_faults.begin(), captured_faults.end(), [](const EntryFault &fault) {
    const std::string display = hierarchy_display(fault.hierarchy);
    return display.find("corrupt_inner.tar") != std::string::npos;
  });

  if (!saw_corrupt) {
    std::cerr << "nested_fault_callback: fault callback did not capture corrupt_inner.tar" << std::endl;
    return false;
  }

  return true;
}

} // namespace

int main() {
  bool all_passed = true;

  const bool broken_nested = test_broken_nested_archive();
  report_result("broken_nested_archive", broken_nested);
  all_passed = all_passed && broken_nested;

  const bool missing_multi = test_missing_multi_volume_part();
  report_result("missing_multi_volume_part", missing_multi);
  all_passed = all_passed && missing_multi;

  const bool injected_fault = test_injected_stream_error();
  report_result("injected_stream_error", injected_fault);
  all_passed = all_passed && injected_fault;

  const bool stream_open_failure = test_stream_archive_open_failure_releases_stream();
  report_result("stream_archive_open_failure", stream_open_failure);
  all_passed = all_passed && stream_open_failure;

  const bool archive_entry_part_switch = test_archive_entry_stream_missing_part_fault();
  report_result("archive_entry_stream_missing_part", archive_entry_part_switch);
  all_passed = all_passed && archive_entry_part_switch;

  const bool nested_fault_callback = test_nested_fault_callback_propagation();
  report_result("nested_fault_callback", nested_fault_callback);
  all_passed = all_passed && nested_fault_callback;

  return all_passed ? 0 : 1;
}
