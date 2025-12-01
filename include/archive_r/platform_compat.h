// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include <sys/types.h>

#if defined(_WIN32)
#  include <sys/stat.h>
#  if !defined(_SSIZE_T_DEFINED)
#    include <BaseTsd.h>
using ssize_t = SSIZE_T;
#    define _SSIZE_T_DEFINED
#  endif
#  if !defined(_MODE_T_DEFINED)
using mode_t = unsigned short; // MSVC does not expose POSIX mode_t by default
#    define _MODE_T_DEFINED
#  endif
#endif
