// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include <sys/types.h>

#if defined(_WIN32)
#  include <sys/stat.h>
#  include <BaseTsd.h>
#  if !defined(_SSIZE_T_DEFINED)
using ssize_t = SSIZE_T;
#    define _SSIZE_T_DEFINED
#  endif
#  if !defined(_MODE_T_DEFINED)
using mode_t = unsigned short; // MSVC does not expose POSIX mode_t by default
#    define _MODE_T_DEFINED
#  endif
#endif

namespace archive_r {

// Expose POSIX-like types within the archive_r namespace.
// - On POSIX platforms, ssize_t/mode_t come from <sys/types.h>.
// - On Windows, platform_compat provides fallback definitions above.
#if defined(_WIN32)
using ssize_t = SSIZE_T;
using mode_t = unsigned short;
#else
using ssize_t = ::ssize_t;
using mode_t = ::mode_t;
#endif

} // namespace archive_r
