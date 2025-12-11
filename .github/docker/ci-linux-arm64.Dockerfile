FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
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
    git \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --upgrade --break-system-packages \
    pip setuptools wheel pybind11 build twine
