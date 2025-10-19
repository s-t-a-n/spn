ARG UBUNTU_VERSION=24.04
ARG ZEPHYR_VERSION=3.7.1
ARG ZEPHYR_SDK_VERSION=0.16.9
ARG GCC_VERSION=14
ARG CLANG_VERSION=20

FROM ubuntu:${UBUNTU_VERSION}

ENV DEBIAN_FRONTEND=noninteractive

ARG ZEPHYR_VERSION
ARG ZEPHYR_SDK_VERSION
ARG GCC_VERSION
ARG CLANG_VERSION

# Tools and dependencies
RUN dpkg --add-architecture i386 \
    && apt-get update && apt-get install -y --no-install-recommends \
    gcc-${GCC_VERSION}-multilib g++-${GCC_VERSION}-multilib linux-libc-dev:i386 \
    make cmake ninja-build ccache \
    git gperf python3 python3-pip device-tree-compiler file xz-utils rsync \
    clang-${CLANG_VERSION} clang-format-${CLANG_VERSION} clang-tidy-${CLANG_VERSION} cppcheck \
    nodejs wget ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Set default cc
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-${GCC_VERSION} 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-${GCC_VERSION} 100 && \
    update-alternatives --install /usr/bin/gcov gcov /usr/bin/gcov-${GCC_VERSION} 100 && \
    update-alternatives --install /usr/bin/clang clang /usr/bin/clang-${CLANG_VERSION} 100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-${CLANG_VERSION} 100

# Install west
RUN pip3 install --no-cache-dir --break-system-packages west

# Install Zephyr Python requirements (base + build-test + run-test only)
RUN cd /tmp && \
    wget -q https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/v${ZEPHYR_VERSION}/scripts/requirements-base.txt && \
    wget -q https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/v${ZEPHYR_VERSION}/scripts/requirements-build-test.txt && \
    wget -q https://raw.githubusercontent.com/zephyrproject-rtos/zephyr/v${ZEPHYR_VERSION}/scripts/requirements-run-test.txt && \
    pip3 install --no-cache-dir --break-system-packages -r requirements-base.txt -r requirements-build-test.txt -r requirements-run-test.txt && \
    rm -f requirements*.txt

# Install Zephyr SDK
RUN cd /opt && \
    wget -q https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${ZEPHYR_SDK_VERSION}/zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64.tar.xz && \
    tar -xf zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64.tar.xz && \
    rm zephyr-sdk-${ZEPHYR_SDK_VERSION}_linux-x86_64.tar.xz && \
    cd zephyr-sdk-${ZEPHYR_SDK_VERSION} && \
    ./setup.sh -h -c

# Set Zephyr SDK environment
ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr
ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-${ZEPHYR_SDK_VERSION}

# Verify installations
RUN echo "=== Tool Versions ===" && \
    gcc --version | head -1 && \
    g++ --version | head -1 && \
    clang-${CLANG_VERSION} --version | head -1 && \
    cmake --version | head -1 && \
    python3 --version && \
    west --version && \
    node --version && \
    clang-format-${CLANG_VERSION} --version && \
    clang-tidy-${CLANG_VERSION} --version && \
    cppcheck --version

WORKDIR /workspace
