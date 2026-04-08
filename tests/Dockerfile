FROM ubuntu:24.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        cmake \
        ninja-build \
        gcc-14 \
        g++-14 \
        git \
        ca-certificates && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

ENV CC=gcc-14 \
    CXX=g++-14 \
    GIT_CONFIG_COUNT=1 \
    GIT_CONFIG_KEY_0=safe.directory \
    GIT_CONFIG_VALUE_0=/src
