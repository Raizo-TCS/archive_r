#!/bin/bash
set -euo pipefail

: "${PY_VERSION:?PY_VERSION must be set (e.g., 3.11)}"
: "${ARCH:?ARCH must be set (x86_64|arm64|universal2)}"
export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"

REPO_ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$REPO_ROOT"

cd bindings/python
python -m pip install --upgrade --break-system-packages pip setuptools wheel pybind11 build delocate

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
pip install --no-index "$target_dir"/*.whl
python -c "import archive_r; print(f'{ARCH} validated {archive_r.__version__}')"
