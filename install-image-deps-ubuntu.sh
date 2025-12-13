#!/bin/bash
set -euo pipefail

pf=""
if [[ "${1:-}" == "--pf" ]]; then
  pf="${2:-}"
  shift 2
fi

export DEBIAN_FRONTEND=noninteractive

if [[ "${pf}" == "arm64-cross" ]]; then
  # Enable arm64 architecture and configure sources:
  # - Restrict default ubuntu.sources to amd64
  # - Add ports.ubuntu.com sources for arm64
  dpkg --add-architecture arm64

  sed -i 's/^Types: deb/Types: deb\nArchitectures: amd64/' /etc/apt/sources.list.d/ubuntu.sources

  cat > /etc/apt/sources.list.d/arm64.list <<'EOF'
# arm64 sources for cross build
Deb: deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble main restricted universe multiverse
Deb: deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-updates main restricted universe multiverse
Deb: deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-security main restricted universe multiverse
EOF

  # NOTE: apt uses "deb" lines; keep file compatible even if comments are present.
  # Replace leading "Deb:" with "deb" to avoid confusion.
  sed -i 's/^Deb: /deb /' /etc/apt/sources.list.d/arm64.list
fi

apt-get update

if [[ "${pf}" == "arm64-cross" ]]; then
  apt-get install -y \
    build-essential \
    crossbuild-essential-arm64 \
    cmake \
    ninja-build \
    pkg-config \
    git \
    python3 \
    python3-pip \
    libarchive-dev:arm64 \
    libbz2-dev:arm64 \
    liblzma-dev:arm64 \
    zlib1g-dev:arm64 \
    libssl-dev:arm64 \
    libxml2-dev:arm64
else
  apt-get install -y \
    sudo \
    python3-venv \
    python3-virtualenv \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    python3-dev \
    python3-pip \
    ruby-dev \
    libarchive-dev \
    libbz2-dev \
    liblzma-dev \
    zlib1g-dev \
    libssl-dev \
    libxml2-dev \
    bzip2 \
    xz-utils \
    git
fi

python3 -m pip install --break-system-packages \
  pybind11 build twine

rm -rf /var/lib/apt/lists/*
