#!/usr/bin/env bash
set -euo pipefail
 
# Minimal dependency bootstrapper for archive_r Python sdist builds.
# Downloads and builds static libs into a prefix; supports optional --host for cross.

PREFIX=""
HOST=""
WORKDIR=""

usage() {
  cat <<'EOF'
Usage: bootstrap_deps.sh --prefix <path> [--host <triple>]
Builds zlib, bzip2, xz, lz4, zstd, libb2, libxml2, openssl, attr, acl, libarchive into the prefix.
Environment:
  TMPDIR          temp workspace (default: mktemp)
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

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig:${PKG_CONFIG_PATH:-}"
export CPPFLAGS="-I$PREFIX/include ${CPPFLAGS:-}"
export LDFLAGS="-L$PREFIX/lib -L$PREFIX/lib64 ${LDFLAGS:-}"
export CFLAGS="${CFLAGS:-}"

PARALLEL="${PARALLEL:-1}"
WORKDIR="${TMPDIR:-}"; [[ -z "$WORKDIR" ]] && WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT
mkdir -p "$PREFIX"

CC_PREFIX=""
AR="ar"
RANLIB="ranlib"
CC="gcc"
if [[ -n "$HOST" ]]; then
  CC_PREFIX="$HOST-"
  CC="$CC_PREFIX"gcc
  AR="$CC_PREFIX"ar
  RANLIB="$CC_PREFIX"ranlib
fi

fetch() {
  local url="$1" out="$2"
  if [[ -f "$out" ]]; then return; fi
  curl -L --fail --retry 3 -o "$out" "$url"
}

extract() {
  local tarball="$1" sub="$2"
  tar xf "$tarball" -C "$WORKDIR"
  echo "$WORKDIR/$sub"
}

build_zlib() {
  local name=zlib-1.3.1
  local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://github.com/madler/zlib/releases/download/v1.3.1/$name.tar.gz" "$tarball"
  local src=$(extract "$tarball" "$name")
  local cflags_safe
  cflags_safe=${CFLAGS:-"-O2 -g -pipe -fno-lto -fno-tree-vectorize"}
  (cd "$src" && CC="${CC_PREFIX}gcc" AR="${CC_PREFIX}ar" RANLIB="${CC_PREFIX}ranlib" CFLAGS="$cflags_safe" ./configure --static --prefix="$PREFIX" && make -j"$PARALLEL" && make install)
}

build_bzip2() {
  local name=bzip2-1.0.8
  local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://sourceware.org/pub/bzip2/$name.tar.gz" "$tarball"
  local src=$(extract "$tarball" "$name")
  # クロス環境ではテスト実行を避けるため、ライブラリとヘッダのみ手動インストール
  (cd "$src" && make -j"$PARALLEL" CC="$CC" AR="$AR" RANLIB="$RANLIB" libbz2.a)
  install -d "$PREFIX/lib" "$PREFIX/include" "$PREFIX/share/man/man1" "$PREFIX/bin"
  install -m 644 "$src"/libbz2.a "$PREFIX/lib/"
  install -m 644 "$src"/bzlib.h "$PREFIX/include/"
  # バイナリはターゲット実行環境が無いと動かないため配布のみ行う
  install -m 755 "$src"/bzip2 "$PREFIX/bin" 2>/dev/null || true
  install -m 755 "$src"/bzip2recover "$PREFIX/bin" 2>/dev/null || true
  install -m 644 "$src"/bzip2.1 "$PREFIX/share/man/man1/" 2>/dev/null || true
}

build_xz() {
  local name=xz-5.6.2
  local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://tukaani.org/xz/$name.tar.gz" "$tarball"
  local src=$(extract "$tarball" "$name")
  local cflags_safe
  # Conservative flags to avoid GCC ICE on manylinux aarch64.
  cflags_safe=${CFLAGS:-"-O0 -g -pipe -fno-lto -fno-tree-vectorize"}
  (cd "$src" && CC="$CC" CFLAGS="$cflags_safe" ./configure --prefix="$PREFIX" --disable-shared --enable-static ${HOST:+--host=$HOST} --disable-lzma-links --disable-xz --disable-xzdec --disable-lzmadec --disable-scripts && make -j1 && make install)
}

build_lz4() {
  local name=lz4-1.10.0
  local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://github.com/lz4/lz4/archive/refs/tags/v1.10.0.tar.gz" "$tarball"
  local src=$(extract "$tarball" "$name")
  (cd "$src" && make -j"$PARALLEL" CC="$CC" AR="$AR" RANLIB="$RANLIB" BUILD_SHARED=no PREFIX="$PREFIX" && make install PREFIX="$PREFIX" BUILD_SHARED=no)
}

build_zstd() {
  local name=zstd-1.5.5
  local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://github.com/facebook/zstd/releases/download/v1.5.5/$name.tar.gz" "$tarball"
  local src=$(extract "$tarball" "$name")
  (cd "$src" && make -j"$PARALLEL" CC="$CC" AR="$AR" RANLIB="$RANLIB" PREFIX="$PREFIX" BUILD_SHARED=0 && make install PREFIX="$PREFIX" BUILD_SHARED=0)
}

build_libb2() {
  local name=libb2-0.98.1
  local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://github.com/BLAKE2/libb2/releases/download/v0.98.1/$name.tar.gz" "$tarball"
  local src=$(extract "$tarball" "$name")
  (cd "$src" && CC="$CC" ./configure --prefix="$PREFIX" --disable-shared --enable-static ${HOST:+--host=$HOST} && make -j"$PARALLEL" && make install)
}

build_libxml2() {
  local name=libxml2-2.13.4
  local tarball="$WORKDIR/$name.tar.xz"
  fetch "https://download.gnome.org/sources/libxml2/2.13/$name.tar.xz" "$tarball"
  local src=$(extract "$tarball" "$name")
  local cflags_safe
  # Conservative flags to avoid GCC ICE observed on manylinux aarch64.
  cflags_safe=${CFLAGS:-"-O1 -g -pipe -fno-lto -fno-tree-vectorize"}
  (cd "$src" && CC="$CC" CFLAGS="$cflags_safe" ./configure --prefix="$PREFIX" --without-python --with-zlib --with-lzma --with-threads --disable-shared --enable-static ${HOST:+--host=$HOST} && make -j1 && make install)
}

build_openssl() {
  local name=openssl-3.4.0
  local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://www.openssl.org/source/$name.tar.gz" "$tarball"
  local src=$(extract "$tarball" "$name")
  local target_conf=""; if [[ -n "$HOST" ]]; then target_conf="linux-aarch64"; fi
  (cd "$src" && ./Configure ${target_conf:+$target_conf} ${HOST:+--cross-compile-prefix=$HOST-} no-shared --prefix="$PREFIX" && make -j"$PARALLEL" && make install_sw)
}

build_attr() {
  local name=attr-2.5.2
  local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://download.savannah.gnu.org/releases/attr/$name.tar.gz" "$tarball"
  local src=$(extract "$tarball" "$name")
  (cd "$src" && CC="$CC" ./configure --prefix="$PREFIX" --disable-shared --enable-static ${HOST:+--host=$HOST} && make -j"$PARALLEL" && make install)
}

build_acl() {
  local name=acl-2.3.2
  local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://download.savannah.gnu.org/releases/acl/$name.tar.gz" "$tarball"
  local src=$(extract "$tarball" "$name")
  (cd "$src" && CC="$CC" ./configure --prefix="$PREFIX" --disable-shared --enable-static ${HOST:+--host=$HOST} && make -j"$PARALLEL" && make install)
}

build_libarchive() {
  local name=libarchive-3.7.4
  local tarball="$WORKDIR/$name.tar.gz"
  fetch "https://www.libarchive.org/downloads/$name.tar.gz" "$tarball"
  local src=$(extract "$tarball" "$name")
  (cd "$src" && CC="$CC" ./configure --prefix="$PREFIX" --disable-shared --enable-static ${HOST:+--host=$HOST} --with-openssl --with-xml2 --with-lzma --with-zlib --with-bz2 --with-zstd --with-lz4 --with-expat=no && make -j"$PARALLEL" && make install)
}

build_zlib
build_bzip2
build_xz
build_lz4
build_zstd
build_libb2
build_libxml2
build_openssl
build_attr
build_acl
build_libarchive

echo "[bootstrap] done: $PREFIX"
