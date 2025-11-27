// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#include "entry_fault_error.h"

#include <cstring>
#include <string>
#include <utility>

namespace archive_r {

EntryFaultError::EntryFaultError(EntryFault fault)
    : std::runtime_error(fault.message)
    , _fault(std::move(fault)) {}

EntryFaultError::EntryFaultError(EntryFault fault, const std::string &internal_message)
    : std::runtime_error(internal_message.empty() ? fault.message : internal_message)
    , _fault(std::move(fault)) {}

EntryFaultError make_entry_fault_error(const std::string &message, PathHierarchy hierarchy, int errno_value) {
    EntryFault fault;
    fault.hierarchy = std::move(hierarchy);
    fault.message = message;
    fault.errno_value = errno_value;
    return EntryFaultError(std::move(fault));
}

std::string format_errno_error(const std::string &prefix, int err) {
    if (err == 0) {
        return prefix;
    }

    std::string message = prefix;
    message.append(": ");
    message.append(std::strerror(err));
    message.append(" (posix errno=");
    message.append(std::to_string(err));
    message.push_back(')');
    return message;
}

std::string format_path_errno_error(const std::string &action, const std::string &path, int err) {
    std::string prefix = action;
    if (!path.empty()) {
        prefix.append(" '");
        prefix.append(path);
        prefix.push_back('\'');
    }
    return format_errno_error(prefix, err);
}

std::string prefer_error_detail(const std::string &detail, const std::string &fallback) { return detail.empty() ? fallback : detail; }

} // namespace archive_r
