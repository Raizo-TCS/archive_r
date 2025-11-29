#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
MANYLINUX_IMAGE="quay.io/pypa/manylinux_2_28_x86_64:latest"
LIBARCHIVE_VERSION="3.7.4"
PYTHON_TAGS=(
  "cp39-cp39"
  "cp310-cp310"
  "cp311-cp311"
  "cp312-cp312"
)
HOST_UID="$(id -u)"
HOST_GID="$(id -g)"

for PYTHON_TAG in "${PYTHON_TAGS[@]}"; do
  echo "[manylinux] Building wheel for ${PYTHON_TAG}"
  docker run --rm \
    -e PYTHON_TAG="${PYTHON_TAG}" \
    -e LIBARCHIVE_VERSION="${LIBARCHIVE_VERSION}" \
    -v "${REPO_ROOT}:/io" \
    "${MANYLINUX_IMAGE}" \
    /bin/bash -c 'set -euo pipefail && \
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
        cargo >/dev/null && \
      mkdir -p /tmp/libarchive && \
      curl -sSL "https://www.libarchive.org/downloads/libarchive-${LIBARCHIVE_VERSION}.tar.xz" -o /tmp/libarchive.tar.xz && \
      tar -xf /tmp/libarchive.tar.xz -C /tmp/libarchive --strip-components=1 && \
      cd /tmp/libarchive && \
      ./configure --prefix=/opt/libarchive-custom >/dev/null && \
      make -j$(nproc) >/dev/null && \
      make install >/dev/null && \
      echo "/opt/libarchive-custom/lib" > /etc/ld.so.conf.d/libarchive_custom.conf && \
      ldconfig && \
      python3 -m pip install --upgrade pip >/dev/null && \
      python3 -m pip install --upgrade build twine virtualenv pybind11 >/dev/null && \
      export PKG_CONFIG_PATH=/opt/libarchive-custom/lib/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}} && \
      export LD_LIBRARY_PATH=/opt/libarchive-custom/lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}} && \
      export LIBRARY_PATH=/opt/libarchive-custom/lib${LIBRARY_PATH:+:${LIBRARY_PATH}} && \
      export CMAKE_PREFIX_PATH=/opt/libarchive-custom${CMAKE_PREFIX_PATH:+:${CMAKE_PREFIX_PATH}} && \
      export LibArchive_ROOT=/opt/libarchive-custom && \
      export CPPFLAGS="${CPPFLAGS:+${CPPFLAGS} }-I/opt/libarchive-custom/include" && \
      export CXXFLAGS="${CXXFLAGS:+${CXXFLAGS} }-pthread -I/opt/libarchive-custom/include" && \
      export LDFLAGS="${LDFLAGS:+${LDFLAGS} }-pthread -L/opt/libarchive-custom/lib" && \
      export CC=/opt/rh/gcc-toolset-14/root/usr/bin/gcc && \
      export CXX=/opt/rh/gcc-toolset-14/root/usr/bin/g++ && \
      cd /io && \
      ./build.sh --rebuild-all --python-only && \
      cd bindings/python && \
      rm -rf dist dist_temp && \
      mkdir -p dist dist_temp && \
      PYBIN=/opt/python/${PYTHON_TAG}/bin && \
      ${PYBIN}/pip install --upgrade pip setuptools wheel pybind11 >/dev/null && \
      ${PYBIN}/pip wheel . -w dist_temp/ --no-deps >/dev/null && \
      ${PYBIN}/pip install auditwheel >/dev/null && \
      mkdir -p dist/${PYTHON_TAG} && \
      auditwheel repair dist_temp/*.whl --plat manylinux_2_28_x86_64 -w dist/${PYTHON_TAG}/ >/dev/null && \
      ${PYBIN}/pip install --no-index dist/${PYTHON_TAG}/*.whl >/dev/null && \
      ${PYBIN}/python -c "import archive_r; print(f\"validated {archive_r.__version__} for ${PYTHON_TAG}\")"'

  docker run --rm \
    -v "${REPO_ROOT}:/io" \
    alpine:3.19 \
    sh -c "chown -R ${HOST_UID}:${HOST_GID} /io/build /io/bindings/python || true"
done

echo "[manylinux] Completed matrix build"
