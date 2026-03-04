# syntax=docker/dockerfile:1
FROM quay.io/pypa/manylinux_2_28_aarch64:latest@sha256:34368babc008d6405b4b74c77868d6a5d15b6010cb953574d1f2bb9184392a9b

ARG LIBARCHIVE_VERSION=3.7.5
ENV LIBARCHIVE_VERSION=${LIBARCHIVE_VERSION} \
    ARCHIVE_R_DEPS_PREFIX=/opt/archive_r_deps \
    PARALLEL=2

# copy dependency builder
COPY bindings/python/tools/build-deps-manylinux.sh /tmp/build-deps-manylinux.sh

# prebuild libarchive and friends
RUN yum -y install clang curl && \
    bash /tmp/build-deps-manylinux.sh --host aarch64-linux-gnu --prefix ${ARCHIVE_R_DEPS_PREFIX} && \
    rm -rf /tmp/build-deps-manylinux.sh /tmp/* /var/cache/yum

# expose python installations shipped by the base image
ENV PATH=/opt/python/cp310-cp310/bin:/opt/python/cp311-cp311/bin:/opt/python/cp312-cp312/bin:/opt/python/cp313-cp313/bin:/opt/python/cp314-cp314/bin:${PATH}
