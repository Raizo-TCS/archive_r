FROM ubuntu:24.04

COPY install-image-deps-ubuntu.sh /tmp/install-image-deps-ubuntu.sh
COPY install-deps-ubuntu.sh /tmp/install-deps-ubuntu.sh
RUN bash /tmp/install-image-deps-ubuntu.sh && rm -f /tmp/install-image-deps-ubuntu.sh /tmp/install-deps-ubuntu.sh
