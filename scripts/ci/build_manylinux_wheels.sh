#!/usr/bin/env bash
set -euo pipefail

: "${PYTHON_TAG:?PYTHON_TAG must be set}" 
: "${LIBARCHIVE_VERSION:?LIBARCHIVE_VERSION must be set}" 

WORKSPACE_DIR="/io"
PYTHON_BINDINGS_DIR="${WORKSPACE_DIR}/bindings/python"
TARGET_DIR="dist/${PYTHON_TAG}"
TARGET_PATH="${PYTHON_BINDINGS_DIR}/${TARGET_DIR}"

install_build_dependencies() {
  yum install -y \
    cmake \
    gcc-c++ \
    make \
    curl \
    tar \
    xz \
    autoconf \
    automake \
    libtool \
    pkgconfig \
    openssl-devel \
    zlib-devel \
    bzip2-devel \
    xz-devel \
    libxml2-devel \
    python3 \
    python3-pip \
    python3-devel \
    rust \
    cargo >/dev/null
}

build_custom_libarchive() {
  mkdir -p /tmp/libarchive
  curl -sSL "https://www.libarchive.org/downloads/libarchive-${LIBARCHIVE_VERSION}.tar.xz" -o /tmp/libarchive.tar.xz
  tar -xf /tmp/libarchive.tar.xz -C /tmp/libarchive --strip-components=1
  pushd /tmp/libarchive >/dev/null
  ./configure --prefix=/opt/libarchive-custom >/dev/null
  make -j"$(nproc)" >/dev/null
  make install >/dev/null
  popd >/dev/null
  echo '/opt/libarchive-custom/lib' > /etc/ld.so.conf.d/libarchive_custom.conf
  ldconfig
}

configure_toolchain_env() {
  export PKG_CONFIG_PATH="/opt/libarchive-custom/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
  export LD_LIBRARY_PATH="/opt/libarchive-custom/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
  export LIBRARY_PATH="/opt/libarchive-custom/lib${LIBRARY_PATH:+:${LIBRARY_PATH}}"
  export CMAKE_PREFIX_PATH="/opt/libarchive-custom${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}}"
  export LibArchive_ROOT="/opt/libarchive-custom"
  export CPPFLAGS="${CPPFLAGS:+${CPPFLAGS} }-I/opt/libarchive-custom/include"
  export CXXFLAGS="${CXXFLAGS:+${CXXFLAGS} }-pthread -I/opt/libarchive-custom/include"
  export LDFLAGS="${LDFLAGS:+${LDFLAGS} }-pthread -L/opt/libarchive-custom/lib"
  export CC="/opt/rh/gcc-toolset-14/root/usr/bin/gcc"
  export CXX="/opt/rh/gcc-toolset-14/root/usr/bin/g++"
}

build_python_bindings() {
  cd "${WORKSPACE_DIR}"
  ./build.sh --rebuild-all --python-only

  cd "${PYTHON_BINDINGS_DIR}"
  rm -rf dist_temp
  mkdir -p dist dist_temp
  rm -rf "${TARGET_PATH}"
  mkdir -p "${TARGET_PATH}"

  PYBIN="/opt/python/${PYTHON_TAG}/bin"
  "${PYBIN}/pip" install --upgrade pip setuptools wheel pybind11 >/dev/null
  "${PYBIN}/pip" wheel . -w dist_temp/ --no-deps >/dev/null
  "${PYBIN}/pip" install auditwheel >/dev/null
  auditwheel repair dist_temp/*.whl --plat manylinux_2_28_x86_64 -w "${TARGET_PATH}/" >/dev/null
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

install_build_dependencies
build_custom_libarchive
python3 -m pip install --upgrade pip >/dev/null
python3 -m pip install --upgrade build twine virtualenv pybind11 >/dev/null
configure_toolchain_env
build_python_bindings
ensure_target_wheels
finalize_artifacts
