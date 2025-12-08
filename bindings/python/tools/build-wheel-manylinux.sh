#!/bin/bash
set -euo pipefail

: "${PYTHON_TAG:?PYTHON_TAG must be set (e.g., cp310-cp310)}"
: "${PLATFORM:?PLATFORM must be set (x86_64|aarch64)}"
LIBARCHIVE_VERSION=${LIBARCHIVE_VERSION:-3.7.5}

# Resolve repository root from tools/ directory
REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"

export PYTHON_TAG PLATFORM LIBARCHIVE_VERSION
"${REPO_ROOT}/.github/scripts/ci/build_manylinux_wheels.sh"
