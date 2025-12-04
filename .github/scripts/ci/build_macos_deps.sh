#!/bin/bash
set -euo pipefail

# Configuration
LIBARCHIVE_VERSION="3.8.4"
XZ_VERSION="5.6.4"
ZSTD_VERSION="1.5.7"

INSTALL_PREFIX="$PWD/libs"
mkdir -p "$INSTALL_PREFIX"
mkdir -p sources

# Function to build CMake project
build_cmake() {
    local name=$1
    local source_dir=$2
    shift 2
    local cmake_args=("$@")

    echo "Building $name..."
    cmake -S "$source_dir" -B "build-$name" \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}" \
        "${cmake_args[@]}"
    
    cmake --build "build-$name" --config Release --parallel
    cmake --install "build-$name"
}

# 1. Build XZ (lzma)
echo "Downloading xz..."
curl -L -o sources/xz.tar.gz "https://github.com/tukaani-project/xz/releases/download/v${XZ_VERSION}/xz-${XZ_VERSION}.tar.gz"
tar xf sources/xz.tar.gz -C sources
build_cmake "xz" "sources/xz-${XZ_VERSION}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DENABLE_NLS=OFF \
    -DENABLE_DOC=OFF

# 2. Build Zstd
echo "Downloading zstd..."
curl -L -o sources/zstd.tar.gz "https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/zstd-${ZSTD_VERSION}.tar.gz"
tar xf sources/zstd.tar.gz -C sources
build_cmake "zstd" "sources/zstd-${ZSTD_VERSION}/build/cmake" \
    -DBUILD_SHARED_LIBS=OFF \
    -DZSTD_BUILD_PROGRAMS=OFF \
    -DZSTD_BUILD_TESTS=OFF

# 3. Build Libarchive
echo "Downloading libarchive..."
curl -L -o sources/libarchive.tar.xz "https://www.libarchive.org/downloads/libarchive-${LIBARCHIVE_VERSION}.tar.xz"
tar xf sources/libarchive.tar.xz -C sources

# Libarchive needs to find xz and zstd
export CMAKE_PREFIX_PATH="$INSTALL_PREFIX"

build_cmake "libarchive" "sources/libarchive-${LIBARCHIVE_VERSION}" \
    -DENABLE_TEST=OFF \
    -DENABLE_COVERAGE=OFF \
    -DENABLE_ACL=OFF \
    -DENABLE_XATTR=OFF \
    -DENABLE_ICONV=OFF \
    -DENABLE_LIBXML2=OFF \
    -DENABLE_EXPAT=OFF \
    -DENABLE_PCREPOSIX=OFF \
    -DENABLE_LZO=OFF \
    -DENABLE_CNG=OFF \
    -DENABLE_TAR=OFF \
    -DENABLE_CPIO=OFF \
    -DENABLE_CAT=OFF \
    -DENABLE_NETTLE=OFF \
    -DENABLE_OPENSSL=OFF \
    -DENABLE_LZ4=OFF \
    -DENABLE_B2=OFF \
    -DENABLE_LZMA=ON \
    -DENABLE_ZSTD=ON

echo "Build complete. Libraries installed to $INSTALL_PREFIX"
