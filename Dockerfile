# Development toolchain image for Quarry.
#
# Matches the packages installed by .github/workflows/ci.yml on ubuntu-latest,
# plus the clang-tidy/clang-format/pre-commit tooling used for local
# formatting and static analysis. This image intentionally does not COPY the
# repository in; it is meant to be used with the repository bind-mounted (see
# docker-compose.yml) so host edits and build output stay in sync with the
# container.

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        clang \
        clang-format \
        clang-tidy \
        cmake \
        git \
        libabsl-dev \
        libprotobuf-dev \
        libyaml-dev \
        pipx \
        protobuf-compiler \
        python3 \
        python3-build \
        python3-pip \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --break-system-packages --no-index \
        --find-links=/usr/share/python-wheels setuptools

ENV PATH="/root/.local/bin:${PATH}"
RUN pipx install pre-commit

WORKDIR /workspace
