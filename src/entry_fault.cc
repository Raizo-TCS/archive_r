// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "archive_r/entry_fault.h"

#include <atomic>
#include <memory>

namespace archive_r {
namespace {
std::shared_ptr<const FaultCallback> g_fault_callback = std::make_shared<const FaultCallback>();
}

void register_fault_callback(FaultCallback callback) {
  auto new_callback = std::make_shared<const FaultCallback>(std::move(callback));
  std::atomic_store_explicit(&g_fault_callback, std::move(new_callback), std::memory_order_release);
}

void dispatch_registered_fault(const EntryFault &fault) {
  auto callback = std::atomic_load_explicit(&g_fault_callback, std::memory_order_acquire);
  if (callback && *callback) {
    (*callback)(fault);
  }
}

} // namespace archive_r
