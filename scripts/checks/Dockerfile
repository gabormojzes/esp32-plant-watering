# The tools the CI jobs in .github/workflows/ci.yml run with. A version changed here changes
# there too.
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# From Ubuntu's archive, as on the CI runner: GCC 14 for the host tests, and Clang 20 with its
# libFuzzer runtime for the fuzz targets. The plain build-essential compilers build the C sources
# where a job names no CC, as the runner's defaults do.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        clang-20 \
        g++-14 \
        gcc-14 \
        git \
        jq \
        libclang-rt-20-dev \
        pipx \
    && rm -rf /var/lib/apt/lists/*

# Exact versions from PyPI, whose released files never change. clang-tidy and clang-format come
# from one LLVM release.
RUN for tool in cmake==3.31.6 clang-tidy==22.1.8 clang-format==22.1.8; do \
        PIPX_HOME=/opt/pipx PIPX_BIN_DIR=/usr/local/bin pipx install "$tool" || exit 1; \
    done
