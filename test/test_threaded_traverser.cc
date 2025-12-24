// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/path_hierarchy.h"
#include "archive_r/traverser.h"

#include <atomic>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace archive_r;

namespace {

struct TraversalStats {
  size_t entries{ 0 };
  size_t files{ 0 };
  size_t max_depth{ 0 };
};

TraversalStats collect_stats(const std::string &archive_path) {
  TraversalStats stats;
  Traverser traverser({ make_single_path(archive_path) });
  for (Entry &entry : traverser) {
    ++stats.entries;
    if (entry.is_file()) {
      ++stats.files;
    }
    if (entry.depth() > stats.max_depth) {
      stats.max_depth = entry.depth();
    }
  }
  return stats;
}

bool verify_parallel_traversers(const std::string &archive_path, size_t thread_count, const TraversalStats &expected) {
  std::atomic<bool> failure{ false };
  std::vector<TraversalStats> per_thread(thread_count);
  std::vector<std::string> errors(thread_count);
  std::vector<std::thread> threads;

  auto worker = [&](size_t index) {
    try {
      TraversalStats stats = collect_stats(archive_path);
      per_thread[index] = stats;
      const bool mismatch = (stats.entries != expected.entries) || (stats.files != expected.files) || (stats.max_depth != expected.max_depth);
      if (mismatch || stats.entries == 0) {
        std::ostringstream oss;
        oss << "Thread " << index << " mismatch: entries=" << stats.entries << ", files=" << stats.files << ", depth=" << stats.max_depth;
        errors[index] = oss.str();
        failure.store(true);
      }
    } catch (const std::exception &ex) {
      std::ostringstream oss;
      oss << "Thread " << index << " threw exception: " << ex.what();
      errors[index] = oss.str();
      failure.store(true);
    } catch (...) {
      std::ostringstream oss;
      oss << "Thread " << index << " threw unknown exception";
      errors[index] = oss.str();
      failure.store(true);
    }
  };

  for (size_t i = 0; i < thread_count; ++i) {
    threads.emplace_back(worker, i);
  }
  for (auto &thread : threads) {
    thread.join();
  }

  if (failure.load()) {
    for (const std::string &err : errors) {
      if (!err.empty()) {
        std::cerr << err << std::endl;
      }
    }
    return false;
  }

  std::cout << "Parallel traversers completed successfully." << " entries=" << expected.entries << " files=" << expected.files << " max_depth=" << expected.max_depth << " threads=" << thread_count
            << std::endl;
  return true;
}

} // namespace

int main(int argc, char **argv) {
  const std::string archive_path = "test_data/stress_test_ultimate.tar.gz";
  const size_t thread_count = 4;

  int iterations = 10;
  if (argc >= 2) {
    try {
      iterations = std::stoi(argv[1]);
    } catch (const std::exception &ex) {
      std::cerr << "Failed to parse iteration count: " << ex.what() << std::endl;
      return 1;
    }
    if (iterations <= 0) {
      std::cerr << "Iteration count must be positive (got " << iterations << ")." << std::endl;
      return 1;
    }
  }

  std::cout << "Collecting baseline traversal stats for: " << archive_path << std::endl;
  const TraversalStats baseline = collect_stats(archive_path);
  if (baseline.entries == 0) {
    std::cerr << "Baseline traversal produced zero entries." << std::endl;
    return 1;
  }

  std::cout << "Baseline: entries=" << baseline.entries << " files=" << baseline.files << " max_depth=" << baseline.max_depth << std::endl;

  for (int iteration = 1; iteration <= iterations; ++iteration) {
    std::cout << "Iteration " << iteration << "/" << iterations << std::endl;
    if (!verify_parallel_traversers(archive_path, thread_count, baseline)) {
      std::cerr << "Parallel traverser verification failed on iteration " << iteration << "." << std::endl;
      return 1;
    }
  }

  std::cout << "Parallel traverser verification succeeded for " << iterations << " iteration(s)." << std::endl;

  return 0;
}
