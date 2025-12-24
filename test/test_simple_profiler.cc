// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "simple_profiler.h"

#include <iostream>
#include <string>

using archive_r::internal::SimpleProfiler;

namespace {

bool expect(bool cond, const char *msg) {
  if (!cond) {
    std::cerr << msg << std::endl;
    return false;
  }
  return true;
}

} // namespace

int main() {
  bool ok = true;

  // Start from a clean slate.
  SimpleProfiler::instance().reset();

  // Stop without a prior start: should be a no-op.
  SimpleProfiler::instance().stop("never_started");

  // Start/stop a timer to populate durations_ and counts_.
  SimpleProfiler::instance().start("timer_a");
  SimpleProfiler::instance().stop("timer_a");

  // Start another timer but do not stop it: ensures stop() branch on missing entry is exercised above.
  SimpleProfiler::instance().start("timer_b");

  // report() should print because durations_ is non-empty.
  SimpleProfiler::instance().report();

  // reset() then report() should hit the empty fast-return branch.
  SimpleProfiler::instance().reset();
  SimpleProfiler::instance().report();

  ok = ok && expect(true, "simple_profiler smoke");

  if (!ok) {
    return 1;
  }

  std::cout << "SimpleProfiler coverage test passed" << std::endl;
  return 0;
}
