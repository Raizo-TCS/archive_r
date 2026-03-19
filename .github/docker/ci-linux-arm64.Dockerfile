FROM ubuntu:24.04@sha256:0d39fcc8335d6d74d5502f6df2d30119ff4790ebbb60b364818d5112d9e3e932

COPY install-deps-ubuntu.sh /tmp/install-deps-ubuntu.sh
RUN bash /tmp/install-deps-ubuntu.sh --clean-apt-lists && rm -f /tmp/install-deps-ubuntu.sh
