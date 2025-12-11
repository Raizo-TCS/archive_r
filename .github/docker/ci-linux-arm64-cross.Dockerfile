FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Enable arm64 architecture
RUN dpkg --add-architecture arm64

# Configure sources:
# 1. Restrict default sources (ubuntu.sources) to amd64
# 2. Add ports.ubuntu.com for arm64
RUN sed -i 's/^Types: deb/Types: deb\nArchitectures: amd64/' /etc/apt/sources.list.d/ubuntu.sources && \
    echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble main restricted universe multiverse" > /etc/apt/sources.list.d/arm64.list && \
    echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-updates main restricted universe multiverse" >> /etc/apt/sources.list.d/arm64.list && \
    echo "deb [arch=arm64] http://ports.ubuntu.com/ubuntu-ports noble-security main restricted universe multiverse" >> /etc/apt/sources.list.d/arm64.list

# Install build tools and dependencies
# We install crossbuild-essential-arm64 for the compiler
# We install library dependencies for arm64
RUN apt-get update && apt-get install -y \
    build-essential \
    crossbuild-essential-arm64 \
    cmake \
    ninja-build \
    pkg-config \
    git \
    python3 \
    python3-pip \
    # Target libraries (arm64)
    libarchive-dev:arm64 \
    libbz2-dev:arm64 \
    liblzma-dev:arm64 \
    zlib1g-dev:arm64 \
    libssl-dev:arm64 \
    libxml2-dev:arm64

# Set up cross-compilation environment variables
ENV CC=aarch64-linux-gnu-gcc
ENV CXX=aarch64-linux-gnu-g++
ENV PKG_CONFIG_PATH=/usr/lib/aarch64-linux-gnu/pkgconfig

# Create a toolchain file for CMake to make it easier
RUN echo "set(CMAKE_SYSTEM_NAME Linux)" > /toolchain.cmake && \
    echo "set(CMAKE_SYSTEM_PROCESSOR aarch64)" >> /toolchain.cmake && \
    echo "set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)" >> /toolchain.cmake && \
    echo "set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)" >> /toolchain.cmake && \
    echo "set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)" >> /toolchain.cmake && \
    echo "set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu /usr)" >> /toolchain.cmake && \
    echo "set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)" >> /toolchain.cmake && \
    echo "set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)" >> /toolchain.cmake && \
    echo "set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)" >> /toolchain.cmake && \
    echo "set(PKG_CONFIG_EXECUTABLE /usr/bin/aarch64-linux-gnu-pkg-config)" >> /toolchain.cmake

ENV CMAKE_TOOLCHAIN_FILE=/toolchain.cmake
