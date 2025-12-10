#!/usr/bin/env bash
set -euo pipefail

# Build shared dependencies for archive_r on Linux/manylinux, with optional --host
# for cross targets. OpenSSL/GMP are intentionally omitted; libarchive is linked
# against nettle built with mini-gmp.

ZLIB_VERSION="${ZLIB_VERSION:-1.3.1}"
BZIP2_VERSION="${BZIP2_VERSION:-1.0.8}"
XZ_VERSION="${XZ_VERSION:-5.6.2}"
LZ4_VERSION="${LZ4_VERSION:-1.10.0}"
ZSTD_VERSION="${ZSTD_VERSION:-1.5.5}"
LIBB2_VERSION="${LIBB2_VERSION:-0.98.1}"
LIBXML2_VERSION="${LIBXML2_VERSION:-2.13.4}"
NETTLE_VERSION="${NETTLE_VERSION:-3.9.1}"
ATTR_VERSION="${ATTR_VERSION:-2.5.2}"
ACL_VERSION="${ACL_VERSION:-2.3.2}"
LIBARCHIVE_VERSION="${LIBARCHIVE_VERSION:-3.7.5}"

PREFIX=""
HOST=""
PARALLEL="${PARALLEL:-}"
if [[ -z "$PARALLEL" ]]; then
  if command -v nproc >/dev/null 2>&1; then
    PARALLEL=$(nproc)
  else
    PARALLEL=2
  fi
fi
WORKDIR="${TMPDIR:-}"
[[ -z "$WORKDIR" ]] && WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

usage() {
  cat <<'EOF'
Usage: build-deps-manylinux.sh --prefix <path> [--host <triple>]
Builds zlib, bzip2, xz, lz4, zstd, libb2, libxml2, nettle(mini-gmp), attr, acl, libarchive.
Environment:
  PARALLEL        make -j (default: 1)
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      PREFIX="$2"; shift 2;;
    --host)
      HOST="$2"; shift 2;;
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

CC_PREFIX=""
# prefer clang when available to avoid cc1 crashes under qemu; install via yum if missing
if command -v clang >/dev/null 2>&1; then
  CC="clang"
else
  if command -v yum >/dev/null 2>&1; then
    yum -y install clang >/dev/null 2>&1 || true
  fi
  if command -v clang >/dev/null 2>&1; then
    CC="clang"
  else
    CC="gcc"
  fi
fi
AR="ar"
RANLIB="ranlib"
if [[ -n "$HOST" ]]; then
  CC_PREFIX="$HOST-"
  if command -v "${CC_PREFIX}clang" >/dev/null 2>&1; then
    CC="${CC_PREFIX}clang"
  elif command -v clang >/dev/null 2>&1; then
    # fallback: generic clang with explicit target
    CC="clang --target=$HOST"
  else
    CC="${CC_PREFIX}gcc"
  fi
fi
AR="${CC_PREFIX}ar"
RANLIB="${CC_PREFIX}ranlib"

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"
export CPPFLAGS="-I$PREFIX/include ${CPPFLAGS:-}"
export LDFLAGS="-L$PREFIX/lib -L$PREFIX/lib64 ${LDFLAGS:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:$PREFIX/lib64:${LD_LIBRARY_PATH:-}"

fetch() {
  local out="$1"
  shift
  if [[ -f "$out" ]]; then return; fi
  # manylinux curl can be older; avoid --retry-all-errors for compatibility
  for url in "$@"; do
    if curl -L --fail --retry 5 --retry-delay 5 --retry-max-time 300 --connect-timeout 10 --max-time 600 -o "$out" "$url"; then
      return 0
    fi
    echo "[fetch] retrying with next mirror after failure: $url" >&2
  done
  echo "[fetch] all mirrors failed for $out" >&2
  return 1
}

extract() {
  local tarball="$1" sub="$2"
  tar xf "$tarball" -C "$WORKDIR"
  echo "$WORKDIR/$sub"
}

build_zlib() {
  local name="zlib-${ZLIB_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "$tarball" \
    "https://mirror.ghproxy.com/https://github.com/madler/zlib/releases/download/v${ZLIB_VERSION}/$name.tar.gz" \
    "https://github.com/madler/zlib/releases/download/v${ZLIB_VERSION}/$name.tar.gz"
  local src; src=$(extract "$tarball" "$name")
  local cflags_safe
  local jobs="$PARALLEL"
  cflags_safe=${CFLAGS:-"-O2 -g -pipe -fno-lto -fno-tree-vectorize"}
  if [[ -n "$HOST" ]]; then
    # Avoid compiling under qemu to sidestep cc1 segfaults; copy system zlib instead
    if command -v yum >/dev/null 2>&1; then
      yum -y install zlib zlib-devel >/dev/null 2>&1 || true
    fi
    install -d "$PREFIX/lib" "$PREFIX/lib64" "$PREFIX/include" "$PREFIX/lib/pkgconfig"
    for libdir in /usr/lib64 /usr/lib; do
      if ls "$libdir"/libz.so* >/dev/null 2>&1; then
        cp -P "$libdir"/libz.so* "$PREFIX/lib/" 2>/dev/null || true
        cp -P "$libdir"/libz.so* "$PREFIX/lib64/" 2>/dev/null || true
        break
      fi
    done
    cp /usr/include/zlib.h "$PREFIX/include/" 2>/dev/null || true
    cp /usr/include/zconf.h "$PREFIX/include/" 2>/dev/null || true
    if [[ -f /usr/lib/pkgconfig/zlib.pc ]]; then
      cp /usr/lib/pkgconfig/zlib.pc "$PREFIX/lib/pkgconfig/"
    elif [[ -f /usr/lib64/pkgconfig/zlib.pc ]]; then
      cp /usr/lib64/pkgconfig/zlib.pc "$PREFIX/lib/pkgconfig/"
    fi
    return
  fi
  (cd "$src" && CC="$CC" AR="$AR" RANLIB="$RANLIB" CFLAGS="$cflags_safe" ./configure --prefix="$PREFIX" ${HOST:+--host=$HOST} && make -j"$jobs" && make install)
  rm -f "$PREFIX/lib/libz.a" "$PREFIX/lib64/libz.a"
}

build_bzip2() {
  local name="bzip2-${BZIP2_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "$tarball" \
    "https://distfiles.macports.org/bzip2/$name.tar.gz" \
    "https://sourceware.org/pub/bzip2/$name.tar.gz"
  local src; src=$(extract "$tarball" "$name")
  local cflags_pic
  cflags_pic=${CFLAGS:-"-O2 -g -pipe -fno-lto -fno-tree-vectorize"}
  cflags_pic+=" -fPIC"
  (cd "$src" && make -f Makefile-libbz2_so CC="$CC" AR="$AR" RANLIB="$RANLIB" CFLAGS="$cflags_pic")
  install -d "$PREFIX/lib" "$PREFIX/include" "$PREFIX/share/man/man1" "$PREFIX/bin"
  install -m 755 "$src"/libbz2.so* "$PREFIX/lib/" 2>/dev/null || true
  if versioned_so=$(ls "$PREFIX/lib"/libbz2.so.*.* 2>/dev/null | head -n1); then
    ln -sf "$(basename "$versioned_so")" "$PREFIX/lib/libbz2.so.1"
    ln -sf "$(basename "$versioned_so")" "$PREFIX/lib/libbz2.so"
  fi
  install -m 644 "$src"/bzlib.h "$PREFIX/include/"
  install -m 755 "$src"/bzip2 "$PREFIX/bin" 2>/dev/null || true
  install -m 755 "$src"/bzip2recover "$PREFIX/bin" 2>/dev/null || true
  install -m 644 "$src"/bzip2.1 "$PREFIX/share/man/man1/" 2>/dev/null || true
}

build_xz() {
  local name="xz-${XZ_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "$tarball" \
    "https://distfiles.macports.org/xz/$name.tar.gz" \
    "https://tukaani.org/xz/$name.tar.gz"
  local src; src=$(extract "$tarball" "$name")
  local cflags_safe
  cflags_safe=${CFLAGS:-"-O0 -g -pipe -fno-lto -fno-tree-vectorize"}
  local cache_vars=()
  if [[ -n "$HOST" ]]; then
    cache_vars+=(
      ac_cv_header_stdint_h=yes
      ac_cv_type_int32_t=yes
      ac_cv_type_uint32_t=yes
      ac_cv_type_int64_t=yes
      ac_cv_type_uint64_t=yes
      ac_cv_type_uintptr_t=yes
      ac_cv_type_uint16_t=yes
    )
  fi
  (cd "$src" && env ${cache_vars[@]:-} CC="$CC" CFLAGS="$cflags_safe" ./configure --prefix="$PREFIX" --enable-shared --disable-static ${HOST:+--host=$HOST} --disable-lzma-links --disable-xz --disable-xzdec --disable-lzmadec --disable-scripts && make -j1 && make install)
}

build_lz4() {
  local name="lz4-${LZ4_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "$tarball" \
    "https://mirror.ghproxy.com/https://github.com/lz4/lz4/archive/refs/tags/v${LZ4_VERSION}.tar.gz" \
    "https://github.com/lz4/lz4/archive/refs/tags/v${LZ4_VERSION}.tar.gz"
  local src; src=$(extract "$tarball" "$name")
  local jobs="$PARALLEL"
  if [[ -n "$HOST" ]]; then
    jobs=1 # limit parallelism under qemu-cross to avoid random build crashes
  fi
  (cd "$src" && make -j"$jobs" CC="$CC" AR="$AR" RANLIB="$RANLIB" BUILD_SHARED=yes BUILD_STATIC=no PREFIX="$PREFIX" && make install PREFIX="$PREFIX" BUILD_SHARED=yes BUILD_STATIC=no)
}

build_zstd() {
  local name="zstd-${ZSTD_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "$tarball" \
    "https://mirror.ghproxy.com/https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/$name.tar.gz" \
    "https://github.com/facebook/zstd/releases/download/v${ZSTD_VERSION}/$name.tar.gz"
  local src; src=$(extract "$tarball" "$name")
  local jobs="$PARALLEL"
  if [[ -n "$HOST" ]]; then
    jobs=1 # avoid qemu instability when cross-building
  fi
  # build only the shared library to avoid qemu crashes in static archives
  (cd "$src/lib" && make -j"$jobs" CC="$CC" AR="$AR" RANLIB="$RANLIB" PREFIX="$PREFIX" BUILD_SHARED=1 BUILD_STATIC=0 libzstd && make PREFIX="$PREFIX" BUILD_SHARED=1 BUILD_STATIC=0 install-shared install-includes install-pc)
}

build_libb2() {
  local name="libb2-${LIBB2_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "$tarball" \
    "https://mirror.ghproxy.com/https://github.com/BLAKE2/libb2/releases/download/v${LIBB2_VERSION}/$name.tar.gz" \
    "https://github.com/BLAKE2/libb2/releases/download/v${LIBB2_VERSION}/$name.tar.gz"
  local src; src=$(extract "$tarball" "$name")
  (cd "$src" && CC="$CC" ./configure --prefix="$PREFIX" --enable-shared --disable-static ${HOST:+--host=$HOST} && make -j"$PARALLEL" && make install)
}

build_libxml2() {
  local name="libxml2-${LIBXML2_VERSION}"; local tarball="$WORKDIR/$name.tar.xz"
  fetch "$tarball" \
    "https://mirror.init7.net/gnome/sources/libxml2/${LIBXML2_VERSION%.*}/$name.tar.xz" \
    "https://download.gnome.org/sources/libxml2/${LIBXML2_VERSION%.*}/$name.tar.xz"
  local src; src=$(extract "$tarball" "$name")
  local cflags_safe
  cflags_safe=${CFLAGS:-"-O1 -g -pipe -fno-lto -fno-tree-vectorize"}
  (cd "$src" && CC="$CC" CFLAGS="$cflags_safe" ./configure --prefix="$PREFIX" --without-python --with-zlib --with-lzma --with-threads --enable-shared --disable-static ${HOST:+--host=$HOST} && make -j1 && make install)
}

build_nettle() {
  local name="nettle-${NETTLE_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "$tarball" \
    "https://mirrors.kernel.org/gnu/nettle/$name.tar.gz" \
    "https://ftp.gnu.org/gnu/nettle/$name.tar.gz"
  local src; src=$(extract "$tarball" "$name")
  (cd "$src" && CC="$CC" ./configure --prefix="$PREFIX" --enable-shared --disable-static --enable-mini-gmp ${HOST:+--host=$HOST} && make -j"$PARALLEL" && make install)
}

build_attr() {
  local name="attr-${ATTR_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "$tarball" \
    "https://mirrors.kernel.org/savannah/attr/$name.tar.gz" \
    "https://download.savannah.gnu.org/releases/attr/$name.tar.gz"
  local src; src=$(extract "$tarball" "$name")
  (cd "$src" && CC="$CC" ./configure --prefix="$PREFIX" --enable-shared --disable-static ${HOST:+--host=$HOST} && make -j"$PARALLEL" && make install)
}

build_acl() {
  local name="acl-${ACL_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "$tarball" \
    "https://mirrors.kernel.org/savannah/acl/$name.tar.gz" \
    "https://download.savannah.gnu.org/releases/acl/$name.tar.gz"
  local src; src=$(extract "$tarball" "$name")
  (cd "$src" && CC="$CC" ./configure --prefix="$PREFIX" --enable-shared --disable-static ${HOST:+--host=$HOST} && make -j"$PARALLEL" && make install)
}

build_libarchive() {
  local name="libarchive-${LIBARCHIVE_VERSION}"; local tarball="$WORKDIR/$name.tar.gz"
  fetch "$tarball" \
    "https://distfiles.macports.org/libarchive/$name.tar.gz" \
    "https://www.libarchive.org/downloads/$name.tar.gz"
  local src; src=$(extract "$tarball" "$name")
  (cd "$src" && CC="$CC" ./configure --prefix="$PREFIX" --enable-shared --disable-static ${HOST:+--host=$HOST} \
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
build_attr
build_acl
build_libarchive

echo "[build-deps-manylinux] done: $PREFIX"
