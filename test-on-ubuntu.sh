#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

TEST_TIMEOUT="${RUN_TESTS_TIMEOUT:-120}"
BINDINGS_TIMEOUT="${RUN_BINDINGS_TIMEOUT:-120}"

python3 ./run_with_timeout.py "$TEST_TIMEOUT" ./run_tests.sh
python3 ./run_with_timeout.py "$BINDINGS_TIMEOUT" ./bindings/ruby/run_binding_tests.sh
python3 ./run_with_timeout.py "$BINDINGS_TIMEOUT" ./bindings/python/run_binding_tests.sh
