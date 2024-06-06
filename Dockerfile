FROM debian:bookworm-slim

RUN apt-get update --quiet && \
    apt-get install --assume-yes \
    # minimal build essentials
    cmake \
    curl \
    g++ \
    gdb \
    git \
    ninja-build \
    wget \
    # mecaps / kdutils specific pkgs
    libcurlpp-dev \
    libmosquittopp-dev \
    libwayland-dev \
    wayland-scanner++ \
    wayland-protocols \
    # slint specific pkgs
    libfontconfig-dev \
    libxcb-shape0-dev \
    libxcb-xfixes0-dev \
    libxcb-xkb-dev \
    libxkbcommon-dev \
    libxkbcommon-x11-dev \
    qtbase5-dev

# Install slint from binary
ARG SLINT_VERSION=1.7.0
ENV SLINT_INSTALL_DIR=/usr/src/slint
WORKDIR $SLINT_INSTALL_DIR
RUN wget --quiet -O - https://github.com/slint-ui/slint/releases/download/v$SLINT_VERSION/Slint-cpp-$SLINT_VERSION-Linux-x86_64.tar.gz | tar xvzf - --strip-components=1

# Set CMake prefix path
ENV CMAKE_PREFIX_PATH=$SLINT_INSTALL_DIR

# Add non-root user and set as default user
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME
    # [Optional] If you need to install software after connecting.
    # && apt-get update \
    # && apt-get install -y sudo \
    # && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    # && chmod 0440 /etc/sudoers.d/$USERNAME

USER $USERNAME
