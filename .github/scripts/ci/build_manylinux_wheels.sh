#!/usr/bin/env bash
set -euo pipefail

: "${PYTHON_TAG:?PYTHON_TAG must be set}" 

WORKSPACE_DIR="/io"
PYTHON_BINDINGS_DIR="${WORKSPACE_DIR}/bindings/python"
TARGET_DIR="dist/${PYTHON_TAG}"
TARGET_PATH="${PYTHON_BINDINGS_DIR}/${TARGET_DIR}"
PYBIN="/opt/python/${PYTHON_TAG}/bin"
DEPS_PREFIX="${ARCHIVE_R_DEPS_PREFIX:-/opt/archive_r_deps}"
DEPS_BUILDER="${WORKSPACE_DIR}/bindings/python/tools/build-deps-manylinux.sh"

ensure_dependencies() {
  if [[ -f "${DEPS_PREFIX}/lib/libarchive.a" || -f "${DEPS_PREFIX}/lib/libarchive.so" || -f "${DEPS_PREFIX}/lib64/libarchive.a" || -f "${DEPS_PREFIX}/lib64/libarchive.so" ]]; then
    return
  fi

  if [[ ! -x "${DEPS_BUILDER}" ]]; then
    echo "Dependency builder not found: ${DEPS_BUILDER}" >&2
    exit 1
  fi

  local args=("--prefix" "${DEPS_PREFIX}")
  if [[ -n "${ARCHIVE_R_DEPS_HOST:-}" ]]; then
    args+=("--host" "${ARCHIVE_R_DEPS_HOST}")
  fi
  bash "${DEPS_BUILDER}" "${args[@]}"
}

configure_toolchain_env() {
  export PKG_CONFIG_PATH="${DEPS_PREFIX}/lib/pkgconfig:${DEPS_PREFIX}/lib64/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
  export LD_LIBRARY_PATH="${DEPS_PREFIX}/lib:${DEPS_PREFIX}/lib64${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
  export LIBRARY_PATH="${DEPS_PREFIX}/lib:${DEPS_PREFIX}/lib64${LIBRARY_PATH:+:${LIBRARY_PATH}}"
  export CMAKE_PREFIX_PATH="${DEPS_PREFIX}${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
  export LibArchive_ROOT="${DEPS_PREFIX}"
  export CPPFLAGS="${CPPFLAGS:+${CPPFLAGS} }-I${DEPS_PREFIX}/include"
  export CXXFLAGS="${CXXFLAGS:+${CXXFLAGS} }-pthread -I${DEPS_PREFIX}/include"
  export LDFLAGS="${LDFLAGS:+${LDFLAGS} }-pthread -L${DEPS_PREFIX}/lib -L${DEPS_PREFIX}/lib64"
}

build_python_bindings() {
  cd "${WORKSPACE_DIR}"
  ./build.sh --rebuild-all --python-only

  cd "${PYTHON_BINDINGS_DIR}"
  rm -rf dist_temp
  mkdir -p dist dist_temp
  rm -rf "${TARGET_PATH}"
  mkdir -p "${TARGET_PATH}"

  "${PYBIN}/pip" install --upgrade pip setuptools wheel pybind11 >/dev/null
  "${PYBIN}/pip" wheel . -w dist_temp/ --no-deps >/dev/null
  "${PYBIN}/pip" install auditwheel >/dev/null

  ARCH=$(uname -m)
  if [[ "$ARCH" == "x86_64" ]]; then
    PLAT_TAG="manylinux_2_28_x86_64"
  elif [[ "$ARCH" == "aarch64" ]]; then
    PLAT_TAG="manylinux_2_28_aarch64"
  else
    echo "Unsupported architecture: $ARCH"
    exit 1
  fi

  auditwheel repair dist_temp/*.whl --plat "${PLAT_TAG}" -w "${TARGET_PATH}/" >/dev/null
  "${PYBIN}/pip" install --no-index "${TARGET_PATH}"/*.whl >/dev/null
  "${PYBIN}/python" -c "import archive_r; print(f'validated {archive_r.__version__}')"
}

ensure_target_wheels() {
  if compgen -G "${TARGET_PATH}/*.whl" >/dev/null; then
    echo "Wheels located in ${TARGET_PATH}"
    return
  fi

  echo "No wheels under ${TARGET_PATH}; scanning fallback roots"
  mapfile -t alt_wheels < <(find /io /tmp /root -maxdepth 6 -type f -name 'archive_r*.whl' 2>/dev/null || true)
  if [[ "${#alt_wheels[@]}" -gt 0 ]]; then
    mkdir -p "${TARGET_PATH}"
    for wheel_path in "${alt_wheels[@]}"; do
      echo "Copying ${wheel_path} -> ${TARGET_PATH}"
      cp "${wheel_path}" "${TARGET_PATH}/"
    done
  else
    echo 'No archive_r wheels found in fallback search'
  fi
}

finalize_artifacts() {
  if compgen -G "${TARGET_PATH}/*.whl" >/dev/null; then
    echo "Wheels ready under ${TARGET_PATH}"
  fi

  if [[ -n "${HOST_UID:-}" && -n "${HOST_GID:-}" ]]; then
    chown -R "${HOST_UID}:${HOST_GID}" "${PYTHON_BINDINGS_DIR}/dist" || true
  fi
}

"${PYBIN}/pip" install --upgrade pip >/dev/null
"${PYBIN}/pip" install --upgrade build twine virtualenv pybind11 >/dev/null
ensure_dependencies
configure_toolchain_env
build_python_bindings
ensure_target_wheels
finalize_artifacts
