#!/bin/bash
set -euo pipefail

export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"
RUN_TESTS_WRAPPER_TIMEOUT=0 python3 ./run_with_timeout.py 120 ./run_tests.sh
python3 ./run_with_timeout.py 120 ./bindings/ruby/run_binding_tests.sh
