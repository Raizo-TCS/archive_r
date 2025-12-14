FROM ubuntu:24.04

COPY install-image-deps-ubuntu.sh /tmp/install-image-deps-ubuntu.sh
RUN bash /tmp/install-image-deps-ubuntu.sh --pf arm64-cross && rm -f /tmp/install-image-deps-ubuntu.sh

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
