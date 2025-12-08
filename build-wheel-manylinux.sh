#!/bin/bash
set -euo pipefail

: "${PYTHON_TAG:?PYTHON_TAG must be set (e.g., cp310-cp310)}"
: "${PLATFORM:?PLATFORM must be set (x86_64|aarch64)}"
LIBARCHIVE_VERSION=${LIBARCHIVE_VERSION:-3.7.4}

# This script is intended to run inside manylinux_2_28 images.
# It delegates to the existing CI helper to build and repair wheels.

export PYTHON_TAG PLATFORM LIBARCHIVE_VERSION
/io/.github/scripts/ci/build_manylinux_wheels.sh
