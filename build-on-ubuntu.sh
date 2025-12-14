#!/bin/bash
set -euo pipefail

ARCH=${ARCH:-host}
PACKAGE_MODE=${PACKAGE_MODE:-full}
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

# Build core + bindings. Cross builds should run under appropriate runner (host or qemu).
if [[ "$PACKAGE_MODE" == "core" ]]; then
  ./build.sh --rebuild-all
elif [[ "$PACKAGE_MODE" == "python" ]]; then
  ./build.sh --rebuild-all --python-only --package-python
elif [[ "$PACKAGE_MODE" == "ruby" ]]; then
  ./build.sh --rebuild-all --with-ruby --package-ruby
elif [[ "$PACKAGE_MODE" == "bindings" ]]; then
  ./build.sh --rebuild-all --with-python --with-ruby
else
  ./build.sh --rebuild-all --package-python --package-ruby
fi
