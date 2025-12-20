#!/bin/bash
set -euo pipefail

: "${PY_VERSION:?PY_VERSION must be set (e.g., 3.11)}"
: "${ARCH:?ARCH must be set (x86_64|arm64|universal2)}"
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"

if [[ -n "${ARCHIVE_R_DEPS_PREFIX:-}" ]]; then
	export LIBARCHIVE_ROOT="${LIBARCHIVE_ROOT:-$ARCHIVE_R_DEPS_PREFIX}"
	export LIBARCHIVE_INCLUDE_DIRS="${LIBARCHIVE_INCLUDE_DIRS:-$ARCHIVE_R_DEPS_PREFIX/include}"
	export LIBARCHIVE_LIBRARY_DIRS="${LIBARCHIVE_LIBRARY_DIRS:-$ARCHIVE_R_DEPS_PREFIX/lib:$ARCHIVE_R_DEPS_PREFIX/lib64}"
	export LIBARCHIVE_RUNTIME_DIRS="${LIBARCHIVE_RUNTIME_DIRS:-$ARCHIVE_R_DEPS_PREFIX/lib:$ARCHIVE_R_DEPS_PREFIX/lib64}"
	export PKG_CONFIG_PATH="$ARCHIVE_R_DEPS_PREFIX/lib/pkgconfig:$ARCHIVE_R_DEPS_PREFIX/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"
	export CPPFLAGS="-I$ARCHIVE_R_DEPS_PREFIX/include ${CPPFLAGS:-}"
	export LDFLAGS="-L$ARCHIVE_R_DEPS_PREFIX/lib -L$ARCHIVE_R_DEPS_PREFIX/lib64 ${LDFLAGS:-}"
fi

REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$REPO_ROOT"

cd bindings/python
python -m pip install --break-system-packages --require-hashes -r tools/requirements-wheel-macos.txt

deployment_tag="${MACOSX_DEPLOYMENT_TARGET//./_}"
if [[ "${ARCH}" == "universal2" ]]; then
	export ARCHFLAGS="-arch x86_64 -arch arm64"
	plat_name="macosx_${deployment_tag}_universal2"
	target_dir="dist/macos-universal2-${PY_VERSION}"
else
	export ARCHFLAGS="-arch ${ARCH}"
	plat_name="macosx_${deployment_tag}_${ARCH}"
	target_dir="dist/macos-${ARCH}-${PY_VERSION}"
fi
mkdir -p "$target_dir"

python -m pip wheel . -w "$target_dir" --no-deps --config-settings="--build-option=--plat-name=${plat_name}"
original_wheel=$(ls "$target_dir"/*.whl | head -n 1)
repair_dir=$(mktemp -d)
delocate-wheel -v -w "$repair_dir" "$original_wheel"
rm -f "$target_dir"/*.whl
mv "$repair_dir"/*.whl "$target_dir"/

# Smoke test
python -m venv .venv
source .venv/bin/activate
wheel_path="$(ls "$target_dir"/*.whl | head -n 1)"
wheel_abs_path="$(python - <<'PY'
import os
import sys
print(os.path.abspath(sys.argv[1]))
PY
"$wheel_path")"
wheel_sha256="$(python - <<'PY'
import hashlib
import sys
h=hashlib.sha256()
with open(sys.argv[1],'rb') as f:
	for chunk in iter(lambda: f.read(1024*1024), b''):
		h.update(chunk)
print(h.hexdigest())
PY
"$wheel_abs_path")"
req_file="$(mktemp)"
printf "%s --hash=sha256:%s\n" "$wheel_abs_path" "$wheel_sha256" > "$req_file"
pip install --no-index --no-deps --require-hashes -r "$req_file"
python -c "import os, archive_r; arch = os.environ.get('ARCH', '?'); print(f'{arch} validated {archive_r.__version__}')"
