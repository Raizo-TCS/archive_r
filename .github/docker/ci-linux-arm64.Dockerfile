FROM ubuntu:24.04@sha256:cd1dba651b3080c3686ecf4e3c4220f026b521fb76978881737d24f200828b2b

COPY install-deps-ubuntu.sh /tmp/install-deps-ubuntu.sh
RUN bash /tmp/install-deps-ubuntu.sh --clean-apt-lists && rm -f /tmp/install-deps-ubuntu.sh
