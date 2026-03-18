# syntax=docker/dockerfile:1
FROM quay.io/pypa/manylinux_2_28_aarch64:latest@sha256:fb4a43425ac4e996a2056ce50f0d3e79df8bf0fa6ef11cc4632d18adfe67a639

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
