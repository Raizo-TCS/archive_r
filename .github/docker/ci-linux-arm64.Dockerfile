FROM ubuntu:24.04@sha256:186072bba1b2f436cbb91ef2567abca677337cfc786c86e107d25b7072feef0c

COPY install-deps-ubuntu.sh /tmp/install-deps-ubuntu.sh
RUN bash /tmp/install-deps-ubuntu.sh --clean-apt-lists && rm -f /tmp/install-deps-ubuntu.sh
