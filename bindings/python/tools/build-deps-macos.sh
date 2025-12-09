#!/usr/bin/env bash
set -euo pipefail

# Build shared dependencies for archive_r on macOS. OpenSSL/GMP are omitted;
# libarchive is linked against nettle built with mini-gmp.

export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-11.0}"
ZLIB_VERSION="${ZLIB_VERSION:-1.3.1}"
BZIP2_VERSION="${BZIP2_VERSION:-1.0.8}"
XZ_VERSION="${XZ_VERSION:-5.6.2}"
LZ4_VERSION="${LZ4_VERSION:-1.10.0}"
ZSTD_VERSION="${ZSTD_VERSION:-1.5.5}"
LIBB2_VERSION="${LIBB2_VERSION:-0.98.1}"
LIBXML2_VERSION="${LIBXML2_VERSION:-2.13.4}"
NETTLE_VERSION="${NETTLE_VERSION:-3.9.1}"
LIBARCHIVE_VERSION="${LIBARCHIVE_VERSION:-3.7.5}"

PREFIX=""
PARALLEL="${PARALLEL:-$(sysctl -n hw.ncpu 2>/dev/null || echo 1)}"
BASE_TMP="${TMPDIR:-/tmp}"
WORKDIR="$(mktemp -d "${BASE_TMP%/}/archive_r_deps.XXXXXX")"
trap 'rm -rf "$WORKDIR"' EXIT

usage() {
  cat <<'EOF'
Usage: build-deps-macos.sh --prefix <path>
Builds zlib, bzip2, xz, lz4, zstd, libb2, libxml2, nettle(mini-gmp), libarchive.
Environment:
  PARALLEL        make -j (default: hw.ncpu)
  MACOSX_DEPLOYMENT_TARGET deployment target (default: 11.0)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      PREFIX="$2"; shift 2;;
    -h|--help)
      usage; exit 0;;
    *)
      echo "unknown arg: $1" >&2; usage; exit 1;;
  esac
done

if [[ -z "$PREFIX" ]]; then
  echo "--prefix is required" >&2
  exit 1
fi

mkdir -p "$PREFIX"

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"
export CPPFLAGS="-I$PREFIX/include ${CPPFLAGS:-}"
export LDFLAGS="-L$PREFIX/lib -L$PREFIX/lib64 ${LDFLAGS:-}"
export LIBARCHIVE_ROOT="$PREFIX"
export LIBARCHIVE_INCLUDE_DIRS="$PREFIX/include"
export LIBARCHIVE_LIBRARY_DIRS="$PREFIX/lib:$PREFIX/lib64"
export LIBARCHIVE_RUNTIME_DIRS="$PREFIX/lib:$PREFIX/lib64"

fetch() {
  local url="$1" out="$2"
  if [[ -f "$out" ]]; then return; fi
  local tries="${FETCH_RETRIES:-5}"
  local base_sleep="${FETCH_RETRY_DELAY:-10}"
  local attempt=1
  while (( attempt <= tries )); do
    if curl -L --fail --connect-timeout 20 --max-time 180 -o "$out" "$url"; then
      return
    fi
    if (( attempt == tries )); then
      return 1
    fi
    local sleep_for=$(( base_sleep * attempt ))
    echo "[fetch] attempt ${attempt}/${tries} failed for ${url}; sleeping ${sleep_for}s before retry" >&2
    sleep "$sleep_for"
    attempt=$((attempt+1))
  done
}

extract() {
  local tarball="$1" sub="$2"
  tar xf "$tarball" -C "$WORKDIR"
  echo "$WORKDIR/$sub"
}

build_zlib() {
  local name="zlib-${ZLIB_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://github.com/madler/zlib/releases/download/v${ZLIB_VERSION}/$name.tar.gz" "$tarball"
  local src; src=$(extract "$tarball" "$name")
  local cflags_safe
  cflags_safe=${CFLAGS:-"-O2 -g -pipe -fno-lto -fno-tree-vectorize"}
  (cd "$src" && CFLAGS="$cflags_safe" ./configure --prefix="$PREFIX" --shared && make -j"$PARALLEL" && make install)
  rm -f "$PREFIX/lib/libz.a"
}

build_bzip2() {
  local name="bzip2-${BZIP2_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://sourceware.org/pub/bzip2/$name.tar.gz" "$tarball"
  local src; src=$(extract "$tarball" "$name")
  
  # Build shared library manually for macOS (upstream Makefile-libbz2_so is Linux-specific)
  (cd "$src" && \
   cc -c -O2 -g -pipe -fPIC -D_FILE_OFFSET_BITS=64 blocksort.c huffman.c crctable.c randtable.c compress.c decompress.c bzlib.c && \
   cc -shared -Wl,-install_name -Wl,"$PREFIX/lib/libbz2.1.0.dylib" -o "libbz2.${BZIP2_VERSION}.dylib" \
      blocksort.o huffman.o crctable.o randtable.o compress.o decompress.o bzlib.o && \
   rm -f libbz2.dylib libbz2.1.0.dylib && \
   ln -s "libbz2.${BZIP2_VERSION}.dylib" libbz2.1.0.dylib && \
   ln -s "libbz2.${BZIP2_VERSION}.dylib" libbz2.dylib && \
   make -j"$PARALLEL" CC=cc CFLAGS="-O2 -g -pipe -fPIC" bzip2 bzip2recover)

  install -d "$PREFIX/lib" "$PREFIX/include" "$PREFIX/share/man/man1" "$PREFIX/bin"
  install -m 755 "$src/libbz2.${BZIP2_VERSION}.dylib" "$PREFIX/lib/"
  (cd "$PREFIX/lib" && ln -sf "libbz2.${BZIP2_VERSION}.dylib" libbz2.1.0.dylib && ln -sf "libbz2.${BZIP2_VERSION}.dylib" libbz2.dylib)
  install -m 644 "$src"/bzlib.h "$PREFIX/include/"
  install -m 755 "$src"/bzip2 "$PREFIX/bin" 2>/dev/null || true
  install -m 755 "$src"/bzip2recover "$PREFIX/bin" 2>/dev/null || true
  install -m 644 "$src"/bzip2.1 "$PREFIX/share/man/man1/" 2>/dev/null || true
}

build_xz() {
  local name="xz-${XZ_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://tukaani.org/xz/$name.tar.gz" "$tarball"
  local src; src=$(extract "$tarball" "$name")
  local cflags_safe
  cflags_safe=${CFLAGS:-"-O0 -g -pipe -fno-lto -fno-tree-vectorize"}
  (cd "$src" && CC=cc CFLAGS="$cflags_safe" ./configure --prefix="$PREFIX" --enable-shared --disable-static --disable-lzma-links --disable-xz --disable-xzdec --disable-lzmadec --disable-scripts && make -j1 && make install)
}

build_lz4() {
  local name="lz4-${LZ4_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://github.com/lz4/lz4/archive/refs/tags/v${LZ4_VERSION}.tar.gz" "$tarball"
  local src; src=$(extract "$tarball" "$name")
  (cd "$src" && make -j"$PARALLEL" CC=cc AR=ar RANLIB=ranlib BUILD_SHARED=yes BUILD_STATIC=no PREFIX="$PREFIX" && make install PREFIX="$PREFIX" BUILD_SHARED=yes BUILD_STATIC=no)
}

build_zstd() {
  local name="zstd-${ZSTD_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/$name.tar.gz" "$tarball"
  local src; src=$(extract "$tarball" "$name")
  (cd "$src" && make -j"$PARALLEL" CC=cc AR=ar RANLIB=ranlib PREFIX="$PREFIX" BUILD_SHARED=1 BUILD_STATIC=0 && make install PREFIX="$PREFIX" BUILD_SHARED=1 BUILD_STATIC=0)
}

build_libb2() {
  local name="libb2-${LIBB2_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://github.com/BLAKE2/libb2/releases/download/v${LIBB2_VERSION}/$name.tar.gz" "$tarball"
  local src; src=$(extract "$tarball" "$name")
  (cd "$src" && CC=cc ./configure --prefix="$PREFIX" --enable-shared --disable-static && make -j"$PARALLEL" && make install)
}

build_libxml2() {
  local name="libxml2-${LIBXML2_VERSION}"; local tarball="$WORKDIR/$name.tar.xz"
  fetch "https://download.gnome.org/sources/libxml2/${LIBXML2_VERSION%.*}/$name.tar.xz" "$tarball"
  local src; src=$(extract "$tarball" "$name")
  local cflags_safe
  cflags_safe=${CFLAGS:-"-O1 -g -pipe -fno-lto -fno-tree-vectorize"}
  (cd "$src" && CC=cc CFLAGS="$cflags_safe" ./configure --prefix="$PREFIX" --without-python --with-zlib --with-lzma --with-threads --enable-shared --disable-static && make -j1 && make install)
}

build_nettle() {
  local name="nettle-${NETTLE_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  local nettle_urls=(
    "https://ftp.gnu.org/gnu/nettle/$name.tar.gz"
    "https://ftpmirror.gnu.org/nettle/$name.tar.gz"
    "https://www.mirrorservice.org/sites/ftp.gnu.org/gnu/nettle/$name.tar.gz"
  )

  local fetched=1
  for url in "${nettle_urls[@]}"; do
    if fetch "$url" "$tarball"; then
      fetched=0
      break
    fi
    echo "[fetch] nettle primary failed, trying fallback..." >&2
  done

  if (( fetched != 0 )); then
    echo "failed to fetch nettle sources from mirrors" >&2
    return 1
  fi

  local src; src=$(extract "$tarball" "$name")
  (cd "$src" && CC=cc ./configure --prefix="$PREFIX" --enable-shared --disable-static --enable-mini-gmp && make -j"$PARALLEL" && make install)
}

build_libarchive() {
  local name="libarchive-${LIBARCHIVE_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://www.libarchive.org/downloads/$name.tar.gz" "$tarball"
  local src; src=$(extract "$tarball" "$name")
  (cd "$src" && CC=cc ./configure --prefix="$PREFIX" --enable-shared --disable-static \
    --with-nettle --without-openssl --without-mbedtls --without-gnutls \
    --with-xml2 --with-lzma --with-zlib --with-bz2 --with-zstd --with-lz4 --with-libb2 \
    --without-iconv --with-expat=no --without-lzo2 --without-cng && make -j"$PARALLEL" && make install)
}

build_zlib
build_bzip2
build_xz
build_lz4
build_zstd
build_libb2
build_libxml2
build_nettle
build_libarchive

if [[ -n "${GITHUB_ENV:-}" ]]; then
  {
    echo "ARCHIVE_R_DEPS_PREFIX=$PREFIX"
    echo "LIBARCHIVE_ROOT=$PREFIX"
    echo "LIBARCHIVE_INCLUDE_DIRS=$PREFIX/include"
    echo "LIBARCHIVE_LIBRARY_DIRS=$PREFIX/lib:$PREFIX/lib64"
    echo "LIBARCHIVE_RUNTIME_DIRS=$PREFIX/lib:$PREFIX/lib64"
    echo "PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    echo "CMAKE_PREFIX_PATH=$PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
    echo "LIBRARY_PATH=$PREFIX/lib:$PREFIX/lib64${LIBRARY_PATH:+:$LIBRARY_PATH}"
    echo "LD_LIBRARY_PATH=$PREFIX/lib:$PREFIX/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    echo "LibArchive_ROOT=$PREFIX"
  } >> "$GITHUB_ENV"
fi

echo "[build-deps-macos] done: $PREFIX"
