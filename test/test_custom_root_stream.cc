// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/data_stream.h"
#include "archive_r/entry.h"
#include "archive_r/path_hierarchy.h"
#include "archive_r/traverser.h"
#include "system_file_stream.h"

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

using namespace archive_r;

namespace {

class RootStreamFactoryGuard {
public:
  RootStreamFactoryGuard()
      : _original(get_root_stream_factory()) {}

  ~RootStreamFactoryGuard() { set_root_stream_factory(_original); }

  RootStreamFactoryGuard(const RootStreamFactoryGuard &) = delete;
  RootStreamFactoryGuard &operator=(const RootStreamFactoryGuard &) = delete;

private:
  RootStreamFactory _original;
};

class InstrumentedRootStream : public IDataStream {
public:
  explicit InstrumentedRootStream(PathHierarchy hierarchy)
      : _inner(std::make_shared<SystemFileStream>(hierarchy))
      , _hierarchy(std::move(hierarchy)) {
    instance_count().fetch_add(1);
    construction_count().fetch_add(1);
  }

  ~InstrumentedRootStream() override { instance_count().fetch_sub(1); }

  ssize_t read(void *buffer, size_t size) override {
    total_reads().fetch_add(1);
    return _inner->read(buffer, size);
  }

  void rewind() override { _inner->rewind(); }

  bool at_end() const override { return _inner->at_end(); }

  PathHierarchy source_hierarchy() const override { return _hierarchy; }

  static void reset_metrics() {
    instance_count().store(0);
    construction_count().store(0);
    total_reads().store(0);
  }

  static int live_instances() { return instance_count().load(); }
  static int constructions() { return construction_count().load(); }
  static int reads() { return total_reads().load(); }

private:
  std::shared_ptr<IDataStream> _inner;
  PathHierarchy _hierarchy;

  static std::atomic<int> &instance_count() {
    static std::atomic<int> counter{ 0 };
    return counter;
  }

  static std::atomic<int> &construction_count() {
    static std::atomic<int> counter{ 0 };
    return counter;
  }

  static std::atomic<int> &total_reads() {
    static std::atomic<int> counter{ 0 };
    return counter;
  }
};

void report_result(const std::string &name, bool success) { std::cout << "[" << (success ? "PASS" : "FAIL") << "] " << name << std::endl; }

bool test_custom_factory_registration() {
  RootStreamFactoryGuard guard;
  InstrumentedRootStream::reset_metrics();
  std::atomic<int> factory_calls{ 0 };

  set_root_stream_factory([&factory_calls](const PathHierarchy &hierarchy) -> std::shared_ptr<IDataStream> {
    factory_calls.fetch_add(1);
    return std::make_shared<InstrumentedRootStream>(hierarchy);
  });

  Traverser traverser({ make_single_path("test_data/deeply_nested.tar.gz") });
  size_t entries = 0;
  for (Entry &entry : traverser) {
    (void)entry;
    ++entries;
  }

  const bool success = (factory_calls.load() == 1) && (entries > 0) && (InstrumentedRootStream::reads() > 0) && (InstrumentedRootStream::live_instances() == 0);
  if (!success) {
    if (factory_calls.load() != 1) {
      std::cerr << "custom_factory_registration: unexpected factory invocation count = " << factory_calls.load() << std::endl;
    }
    if (entries == 0) {
      std::cerr << "custom_factory_registration: traverser enumerated zero entries" << std::endl;
    }
    if (InstrumentedRootStream::reads() == 0) {
      std::cerr << "custom_factory_registration: instrumented stream never read" << std::endl;
    }
    if (InstrumentedRootStream::live_instances() != 0) {
      std::cerr << "custom_factory_registration: instrumented stream leak detected" << std::endl;
    }
  }

  return success;
}

bool test_default_factory_fallback() {
  RootStreamFactoryGuard guard;
  set_root_stream_factory(RootStreamFactory{});

  Traverser traverser({ make_single_path("test_data/deeply_nested.tar.gz") });
  size_t entries = 0;
  for (Entry &entry : traverser) {
    (void)entry;
    ++entries;
  }

  if (entries == 0) {
    std::cerr << "default_factory_fallback: traversal failed when factory cleared" << std::endl;
    return false;
  }
  return true;
}

bool test_thread_safe_factory_registration() {
  RootStreamFactoryGuard guard;
  std::atomic<bool> failure{ false };

  auto worker = [&failure]() {
    for (int i = 0; i < 200; ++i) {
      RootStreamFactory factory = [](const PathHierarchy &hierarchy) -> std::shared_ptr<IDataStream> { return std::make_shared<SystemFileStream>(hierarchy); };
      set_root_stream_factory(factory);
      RootStreamFactory snapshot = get_root_stream_factory();
      if (!snapshot) {
        failure.store(true);
        return;
      }
      auto stream = snapshot(make_single_path("test_data/deeply_nested.tar.gz"));
      if (!stream) {
        failure.store(true);
        return;
      }
    }
  };

  std::vector<std::thread> threads;
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back(worker);
  }
  for (auto &thread : threads) {
    thread.join();
  }

  return !failure.load();
}

} // namespace

int main() {
  bool all_passed = true;

  const bool custom_factory = test_custom_factory_registration();
  report_result("custom_root_stream_factory", custom_factory);
  all_passed = all_passed && custom_factory;

  const bool default_fallback = test_default_factory_fallback();
  report_result("root_stream_default_fallback", default_fallback);
  all_passed = all_passed && default_fallback;

  const bool concurrent_registration = test_thread_safe_factory_registration();
  report_result("root_stream_factory_thread_safety", concurrent_registration);
  all_passed = all_passed && concurrent_registration;

  return all_passed ? 0 : 1;
}
