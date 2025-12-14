#!/bin/bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

usage() {
  cat <<'EOF'
Usage: install-deps-ubuntu.sh [--cross] [--arch <arch>] [--clean-apt-lists]

Options:
  --cross              Install cross toolchain (requires --arch)
  --arch <arch>        Target architecture for lib*-dev packages (e.g. arm64)
  --clean-apt-lists    Remove /var/lib/apt/lists/* after install (image use)
EOF
}

cross=0
arch=""
clean_apt_lists=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cross)
      cross=1
      shift
      ;;
    --arch)
      arch="${2:-}"
      shift 2
      ;;
    --arch=*)
      arch="${1#--arch=}"
      shift
      ;;
    --clean-apt-lists)
      clean_apt_lists=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ ${cross} -eq 1 && -z "${arch}" ]]; then
  echo "--cross requires --arch" >&2
  exit 2
fi

pkg_with_arch() {
  local pkg="$1"
  if [[ -n "${arch}" ]]; then
    echo "${pkg}:${arch}"
  else
    echo "${pkg}"
  fi
}

if [[ -n "${arch}" ]]; then
  dpkg --add-architecture "${arch}"

  if [[ "${arch}" == "arm64" && -f /etc/apt/sources.list.d/ubuntu.sources ]]; then
    # - Restrict default ubuntu.sources to amd64
    # - Add ports.ubuntu.com sources for arm64
    sed -i 's/^Types: deb/Types: deb\nArchitectures: amd64/' /etc/apt/sources.list.d/ubuntu.sources

    cat > /etc/apt/sources.list.d/arm64.list <<'EOF'
# arm64 sources for cross build
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-updates main restricted universe multiverse
deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-security main restricted universe multiverse
EOF
  fi
fi

apt-get update

base_packages=(
  build-essential
  cmake
  ninja-build
  pkg-config
  python3
  python3-dev
  python3-pip
  python3-venv
  ruby-dev
)

lib_dev_packages=(
  libarchive-dev
  libbz2-dev
  liblzma-dev
  zlib1g-dev
  libssl-dev
  libxml2-dev
)

packages=("${base_packages[@]}")

if [[ ${cross} -eq 1 ]]; then
  packages+=("crossbuild-essential-${arch}")
fi

for pkg in "${lib_dev_packages[@]}"; do
  packages+=("$(pkg_with_arch "${pkg}")")
done

apt-get install -y "${packages[@]}"

if [[ ${clean_apt_lists} -eq 1 ]]; then
  rm -rf /var/lib/apt/lists/*
fi

