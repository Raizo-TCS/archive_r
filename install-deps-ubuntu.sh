#!/bin/bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  python3-dev \
  python3-pip \
  python3-venv \
  ruby-dev \
  libarchive-dev \
  libbz2-dev \
  liblzma-dev \
  zlib1g-dev \
  libssl-dev \
  libxml2-dev \
  bzip2 \
  xz-utils

