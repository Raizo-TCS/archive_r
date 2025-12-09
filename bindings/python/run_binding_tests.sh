#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BINDING_DIR="$ROOT_DIR/bindings/python"
BUILD_DIR="$ROOT_DIR/build"
LOG_DIR="$BUILD_DIR/logs"

info() { echo "[python-binding] $*"; }
warn() { echo "[python-binding][warn] $*" >&2; }

if [ ! -d "$BINDING_DIR" ]; then
    info "Python binding sources not found - skipping Python tests"
    exit 0
fi

mkdir -p "$LOG_DIR"
shopt -s nullglob
py_exts=("$BINDING_DIR"/*.so "$BINDING_DIR"/*.pyd)
if [ ${#py_exts[@]} -eq 0 ]; then
    info "Python binding not built - skipping Python tests"
    exit 0
fi

# Copy DLLs for Windows runtime dependency resolution
# Python 3.8+ on Windows does not search PATH for DLLs, so we copy them next to the .pyd
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" || "$OSTYPE" == "win32" ]]; then
    info "Copying DLLs to binding directory for Windows execution..."
    cp "$BUILD_DIR"/*.dll "$BINDING_DIR" 2>/dev/null || true
    cp "$BUILD_DIR"/Release/*.dll "$BINDING_DIR" 2>/dev/null || true
    cp "$BUILD_DIR"/core/Release/*.dll "$BINDING_DIR" 2>/dev/null || true
fi

python_cmd="python3"
if ! command -v python3 >/dev/null 2>&1 && command -v python >/dev/null 2>&1; then
    python_cmd="python"
fi

if ! command -v "$python_cmd" >/dev/null 2>&1; then
    warn "Python interpreter not found - skipping Python binding tests"
    exit 0
fi

pushd "$BINDING_DIR" >/dev/null
if "$python_cmd" test/test_traverser.py > "$LOG_DIR/python_test.log" 2>&1; then
    info "Python binding tests passed"
else
    warn "Python binding tests failed"
    echo "--- Python Test Output ---"
    cat "$LOG_DIR/python_test.log"
    echo "--------------------------"
    popd >/dev/null
    exit 1
fi
popd >/dev/null
info "Python binding tests completed"
