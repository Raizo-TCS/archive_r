#!/bin/bash
set -euo pipefail

export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
export HOMEBREW_NO_AUTO_UPDATE=1
export HOMEBREW_NO_INSTALL_CLEANUP=1

brew install cmake ninja pkg-config libarchive ruby coreutils

# python@3.11 can fail with link conflicts during brew install, so handle separately
if ! brew install python@3.11; then
	echo "brew install python@3.11 failed; attempting link overwrite" >&2
	brew link --overwrite python@3.11 || true
fi

PY311_BIN="$(brew --prefix python@3.11)/bin/python3.11"
if [[ ! -x "${PY311_BIN}" ]]; then
	echo "python@3.11 not found after install" >&2
	exit 1
fi

LIBARCHIVE_ROOT="$(brew --prefix libarchive)"
echo "LIBARCHIVE_ROOT=${LIBARCHIVE_ROOT}" >> "$GITHUB_ENV"
echo "CMAKE_PREFIX_PATH=${LIBARCHIVE_ROOT}" >> "$GITHUB_ENV"
echo "LDFLAGS=-L${LIBARCHIVE_ROOT}/lib" >> "$GITHUB_ENV"
echo "CPPFLAGS=-I${LIBARCHIVE_ROOT}/include" >> "$GITHUB_ENV"
echo "PKG_CONFIG_PATH=${LIBARCHIVE_ROOT}/lib/pkgconfig" >> "$GITHUB_ENV"

"${PY311_BIN}" -m pip install --upgrade --break-system-packages pip setuptools wheel pybind11 build twine
