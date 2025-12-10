#!/bin/bash
set -euo pipefail

APT_PREFIX="sudo"
if ! command -v sudo >/dev/null 2>&1; then
  APT_PREFIX=""
fi

${APT_PREFIX} apt-get update
${APT_PREFIX} DEBIAN_FRONTEND=noninteractive apt-get install -y \
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
  xz-utils

PIP_BREAK_SYSTEM_PACKAGES=1 python3 -m pip install --upgrade --ignore-installed \
  pip setuptools wheel pybind11 build twine --break-system-packages
