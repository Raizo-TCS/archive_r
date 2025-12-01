// SPDX-License-Identifier: MIT
// Copyright (c) 2025 archive_r Team

#pragma once

#include <sys/types.h>

#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#  include <BaseTsd.h>
using ssize_t = SSIZE_T;
#  define _SSIZE_T_DEFINED
#endif
