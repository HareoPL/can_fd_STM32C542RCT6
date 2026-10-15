FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

# 1. Wylaczenie sprawdzania certyfikatow SSL (dla sieci korporacyjnych/proxy)
RUN echo 'Acquire::https::Verify-Peer "false";' > /etc/apt/apt.conf.d/99verify-peer.conf

# 2. Instalacja narzedzi bazowych, QA, dokumentacji, bibliotek GUI i zaleznosci sprzetowych
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get -y install --no-install-recommends \
    build-essential \
    ninja-build \
    make \
    git \
    python3 \
    python3-pip \
    python3-venv \
    python3-pytest \
    python3-serial \
    python3-usb \
    python3-pandas \
    python3-numpy \
    python3-openpyxl \
    python3-cryptography \
    python3-pyqt6 \
    python3-pyqt5 \
    libgl1 \
    libglx-mesa0 \
    libegl1 \
    libxkbcommon-x11-0 \
    libxcb-cursor0 \
    can-utils \
    usbutils \
    udev \
    libusb-1.0-0-dev \
    libusb-1.0-0 \
    stlink-tools \
    openocd \
    gdb-multiarch \
    wget \
    curl \
    ca-certificates \
    clangd \
    clang-format \
    cppcheck \
    doxygen \
    graphviz \
    srecord \
    lcov \
    libncurses6 \
    libtinfo6 \
    xz-utils \
    autoconf \
    automake \
    libtool \
    pkg-config \
    telnet \
    netcat-openbsd \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# 3. Instalacja pakietow Python dla Embedded GUI, CAN, telemetrii i Secure Boot
RUN python3 -m pip install --no-cache-dir --break-system-packages \
    pyqtgraph \
    gs-usb \
    cantools \
    python-can \
    imgtool

# 4. Instalacja CMake >= 3.30 (wymog: CMakeLists.txt wymaga cmake_minimum_required 3.30)
ARG CMAKE_VERSION=3.31.5
RUN wget --no-check-certificate -qO- "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" | \
    tar -xz -C /usr/local --strip-components=1

# 5. Instalacja Toolchaina ARM GNU 14.2 (zgodny z cmake/GCC.14.3.1.cmake i STM32C5 Cortex-M33)
ARG ARM_TOOLCHAIN_VERSION=14.2.rel1
RUN wget --no-check-certificate -qO- "https://developer.arm.com/-/media/Files/downloads/gnu/${ARM_TOOLCHAIN_VERSION}/binrel/arm-gnu-toolchain-${ARM_TOOLCHAIN_VERSION}-x86_64-arm-none-eabi.tar.xz" | \
    tar -xJ -C /usr/local --strip-components=1 && \
    ln -sf /usr/local/bin/arm-none-eabi-gdb /usr/bin/gdb-multiarch

# 6. Reguly udev dla ST-Link (ST-LINK V1, V2, V2-1, V3 / V3-SET / V3-MINI)
RUN mkdir -p /etc/udev/rules.d && \
    printf '%s\n' \
    '# STMicroelectronics ST-LINK v1, v2, v2-1, v3 rules' \
    'SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3744", MODE="0666", GROUP="plugdev"' \
    'SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3748", MODE="0666", GROUP="plugdev"' \
    'SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374a", MODE="0666", GROUP="plugdev"' \
    'SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374b", MODE="0666", GROUP="plugdev"' \
    'SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374d", MODE="0666", GROUP="plugdev"' \
    'SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374e", MODE="0666", GROUP="plugdev"' \
    'SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="374f", MODE="0666", GROUP="plugdev"' \
    'SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3752", MODE="0666", GROUP="plugdev"' \
    'SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3753", MODE="0666", GROUP="plugdev"' \
    'SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3754", MODE="0666", GROUP="plugdev"' \
    'SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3755", MODE="0666", GROUP="plugdev"' \
    'SUBSYSTEMS=="usb", ATTRS{idVendor}=="0483", ATTRS{idProduct}=="3757", MODE="0666", GROUP="plugdev"' \
    > /etc/udev/rules.d/99-stlink.rules && \
    groupadd -f plugdev && groupadd -f dialout && \
    usermod -aG plugdev,dialout root

ENV PYQTGRAPH_QT_LIB=PyQt5

WORKDIR /workspaces/5_v-box

