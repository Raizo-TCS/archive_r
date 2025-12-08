build_cmake "libarchive" "sources/libarchive-${LIBARCHIVE_VERSION}" \
echo "Build complete. Libraries installed to $INSTALL_PREFIX"
#!/bin/bash
set -euo pipefail

export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DEPS_PREFIX="${ARCHIVE_R_DEPS_PREFIX:-$REPO_ROOT/libs}"

bash "$REPO_ROOT/bindings/python/tools/build-deps-macos.sh" --prefix "$DEPS_PREFIX"
