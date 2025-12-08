#!/bin/bash
set -euo pipefail

export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
export HOMEBREW_NO_AUTO_UPDATE=1
export HOMEBREW_NO_INSTALL_CLEANUP=1

brew install cmake ninja pkg-config libarchive ruby python@3.11 coreutils

LIBARCHIVE_ROOT="$(brew --prefix libarchive)"
echo "LIBARCHIVE_ROOT=${LIBARCHIVE_ROOT}" >> "$GITHUB_ENV"
echo "CMAKE_PREFIX_PATH=${LIBARCHIVE_ROOT}" >> "$GITHUB_ENV"
echo "LDFLAGS=-L${LIBARCHIVE_ROOT}/lib" >> "$GITHUB_ENV"
echo "CPPFLAGS=-I${LIBARCHIVE_ROOT}/include" >> "$GITHUB_ENV"
echo "PKG_CONFIG_PATH=${LIBARCHIVE_ROOT}/lib/pkgconfig" >> "$GITHUB_ENV"

python3 -m pip install --upgrade pip setuptools wheel pybind11 build twine
