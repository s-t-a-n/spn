# Why a custom image:
# 1) Speed: no apt/pip on every run; jobs start ready.
# 2) Reproducibility: identical toolchain every time (clang-19, cmake, ninja, west, gcovr, SDK).
# 3) Stability: no random apt mirror hiccups mid-build.
# 4) Local parity: "act" uses the same image you define here; no external registry needed.
# 5) Simpler CI: workflows just "docker run" your build; fewer moving parts.

ARG ZEPHYR_VERSION=v3.7.1
ARG ZEPHYR_SDK_VERSION=0.16.8

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
ARG ZEPHYR_VERSION
ARG ZEPHYR_SDK_VERSION

# Base toolchain
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential gcc-multilib g++-multilib \
    git curl wget xz-utils ca-certificates rsync gnupg \
    cmake ninja-build python3 python3-venv python3-pip \
    cppcheck ccache \
    device-tree-compiler \
    gosu \
    && rm -rf /var/lib/apt/lists/*

# Add LLVM 21 repository and install clang tools
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add - && \
    echo "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-21 main" > /etc/apt/sources.list.d/llvm-21.list && \
    apt-get update && apt-get install -y --no-install-recommends \
    clang-21 clang-format-21 clang-tidy-21 \
    && rm -rf /var/lib/apt/lists/*

# Python tooling in a venv (avoids PEP 668 block)
RUN python3 -m venv /opt/venv
ENV PATH=/opt/venv/bin:$PATH
RUN python -m pip install --upgrade pip && \
    pip install "west==1.5.0" "gcovr==7.*"

# Install Zephyr Python requirements
RUN curl -fL -o /tmp/requirements-base.txt \
    "https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/${ZEPHYR_VERSION}/scripts/requirements-base.txt" && \
    pip install -r /tmp/requirements-base.txt && \
    rm /tmp/requirements-base.txt

# Zephyr SDK (auto-pick linux-x86_64 vs linux-aarch64)
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-${ZEPHYR_SDK_VERSION}
RUN set -eux; \
    arch="$(dpkg --print-architecture)"; \
    case "$arch" in \
      amd64) sdk_arch="x86_64";; \
      arm64) sdk_arch="aarch64";; \
      *) echo "Unsupported arch: $arch" >&2; exit 1;; \
    esac; \
    mkdir -p "${ZEPHYR_SDK_INSTALL_DIR}"; \
    curl -fL -o /tmp/sdk.tar.xz "https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-${sdk_arch}.tar.xz"; \
    tar -xJf /tmp/sdk.tar.xz -C "${ZEPHYR_SDK_INSTALL_DIR}" --strip-components=1; \
    "${ZEPHYR_SDK_INSTALL_DIR}/setup.sh" -t arm-zephyr-eabi -t x86_64-zephyr-elf -c; \
    rm -f /tmp/sdk.tar.xz

# Non-root runtime user (UID/GID provided by build args)
ARG USER_NAME=runner
ARG USER_UID=1000
ARG USER_GID=1000

# Remove ubuntu user if it exists
RUN if getent passwd ubuntu >/dev/null; then \
      userdel ubuntu; \
    fi

# Remove ubuntu group if it exists
RUN if getent group ubuntu >/dev/null; then \
      groupdel ubuntu; \
    fi

# Create runner group
RUN groupadd --gid ${USER_GID} ${USER_NAME}

# Create runner user
RUN useradd --uid ${USER_UID} --gid ${USER_GID} -m -s /bin/bash ${USER_NAME}

# Create cache directories and set permissions
RUN mkdir -p /home/${USER_NAME}/.cache/zephyr /home/${USER_NAME}/.cache/ccache && \
    chown -R ${USER_UID}:${USER_GID} /home/${USER_NAME}

COPY entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENV PATH=/opt/venv/bin:$PATH
ENV HOME=/home/${USER_NAME}
ENV CCACHE_DIR=${HOME}/.cache/ccache
ENV RUNNER_USER=${USER_NAME}
ENV RUNNER_UID=${USER_UID}
ENV RUNNER_GID=${USER_GID}

WORKDIR /work

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
CMD ["/bin/bash"]
