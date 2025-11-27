// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include "archive_r/path_hierarchy.h"

#include <functional>
#include <string>

namespace archive_r {

/** Describes a recoverable failure encountered while visiting an entry. */
struct EntryFault {
  PathHierarchy hierarchy; ///< Path hierarchy where the fault occurred
  std::string message;     ///< Human readable description
  int errno_value = 0;     ///< Optional errno captured from the failing API
};

/** Callback signature used to surface EntryFault notifications. */
using FaultCallback = std::function<void(const EntryFault &)>;

/**
 * @brief Register a global callback to receive EntryFault notifications.
 * Pass an empty std::function to clear the callback.
 */
void register_fault_callback(FaultCallback callback);

/**
 * @brief Dispatch a fault through the globally registered callback, if any.
 */
void dispatch_registered_fault(const EntryFault &fault);

} // namespace archive_r
