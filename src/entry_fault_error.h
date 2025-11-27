// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include "archive_r/entry_fault.h"

#include <stdexcept>
#include <string>

namespace archive_r {

class EntryFaultError : public std::runtime_error {
public:
  explicit EntryFaultError(EntryFault fault);
  EntryFaultError(EntryFault fault, const std::string &internal_message);

  const EntryFault &fault() const noexcept { return _fault; }
  const PathHierarchy &hierarchy() const noexcept { return _fault.hierarchy; }
  int errno_value() const noexcept { return _fault.errno_value; }

private:
  EntryFault _fault;
};

EntryFaultError make_entry_fault_error(const std::string &message, PathHierarchy hierarchy = {}, int errno_value = 0);

std::string format_errno_error(const std::string &prefix, int err);
std::string format_path_errno_error(const std::string &action, const std::string &path, int err);
std::string prefer_error_detail(const std::string &detail, const std::string &fallback);

} // namespace archive_r
