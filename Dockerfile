FROM debian:bookworm-slim

RUN apt-get update --quiet && \
    apt-get install --assume-yes \
    # minimal build essentials
    cmake \
    curl \
    g++ \
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
ARG SLINT_VERSION=1.6.0
ENV SLINT_INSTALL_DIR=/usr/src/slint
WORKDIR $SLINT_INSTALL_DIR
RUN wget --quiet -O - https://github.com/slint-ui/slint/releases/download/v$SLINT_VERSION/Slint-cpp-$SLINT_VERSION-Linux-x86_64.tar.gz | tar xvzf - --strip-components=1

# Set CMake prefix path
ENV CMAKE_PREFIX_PATH=$SLINT_INSTALL_DIR

# Optional: Setup VNC (requires complementary config in devcontainer.json to be enabled also)
# (Configures slint to use Qt backend and sets Qt Platform Plugin to VNC)
# ENV SLINT_BACKEND=qt
# ENV QT_QPA_PLATFORM=vnc
