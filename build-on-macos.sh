#!/bin/bash
set -euo pipefail

export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
PACKAGE_MODE=${PACKAGE_MODE:-full}
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT_DIR"

if [[ "$PACKAGE_MODE" == "core" ]]; then
  ./build.sh --rebuild-all
elif [[ "$PACKAGE_MODE" == "python" ]]; then
  ./build.sh --rebuild-all --python-only --package-python
elif [[ "$PACKAGE_MODE" == "ruby" ]]; then
  ./build.sh --rebuild-all --with-ruby --package-ruby
else
  ./build.sh --rebuild-all --package-python --package-ruby
fi
