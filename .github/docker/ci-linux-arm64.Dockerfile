FROM ubuntu:24.04@sha256:c4a8d5503dfb2a3eb8ab5f807da5bc69a85730fb49b5cfca2330194ebcc41c7b

COPY install-deps-ubuntu.sh /tmp/install-deps-ubuntu.sh
RUN bash /tmp/install-deps-ubuntu.sh --clean-apt-lists && rm -f /tmp/install-deps-ubuntu.sh
