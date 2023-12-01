# MECAPS

**M**odern **E**mbedded **C**++ **AP**plication **S**tarter.

A template for a C++ application using
KDUtils as cross-platform application core library, Slint for the user interface and CMake for the build system.

## About

This starter pack helps you to get started developing a C++ application with KDUtils as cross-platform application core library and Slint as toolkit for the user interface.

The starter pack comes with the following integrations that are all hooked into KDFoundations' event loop and come with neat APIs using KDBindings providing properties, signals and slots:

* curl integration
* mosquito integration

## Prerequisites

In order to use this template and build a C++ application, you need to install a few tools:

  * **[cmake](https://cmake.org/download/)** (3.21 or newer)
  * **[git](https://git-scm.com/)** (to fetch dependencies)
  * **[slint](https://slint-ui.com/docs/cpp/cmake.html#binary-packages)** (optional)
  * C++ compiler that supports C++ 20

If your target environment is Linux or Windows on an x86-64 architecture, then you may also opt into downloading one of Slint's binary packages. These are pre-compiled and require no further tools. You can find setup instructions and download links at

<https://slint-ui.com/docs/cpp/cmake.html#binary-packages>

Alternatively, this template will automatically download the Slint sources and compile them. This option requires you to install Rust by following the [Rust Getting Started Guide](https://www.rust-lang.org/learn/get-started). Once this is done, you should have the ```rustc``` compiler and the ```cargo``` build system installed in your path.

### Linux

Some packages are needed on Linux to compile mecaps and its dependencies. On Debian/Ubuntu these
are:

```
sudo apt install build-essential cmake git libxkbcommon-dev libxcb-xkb-dev libxkbcommon-x11-dev wayland-scanner++ wayland-protocols libwayland-dev libmosquittopp-dev
```

## Usage

1. Clone or download this repository
    ```
    git clone "https://<user.name>@codereview.kdab.com/a/kdab/mecaps" my-project
    cd my-project
    ```
2. Configure with CMake
   ```
   mkdir build
   cmake -B build
   ```
3. Build with CMake
   ```
   cmake --build build
   ```
4. Run the application binary
    * Linux/macOS:
        ```
        ./build/demo/mecaps_demo_ui
        ```
    * Windows:
        ```
        build\demo\mecaps_demo_ui.exe
        ```

## Licensing

Mecaps is (C) 2023 Klarälvdalens Datakonsult AB, and is available under
the terms of the [MIT](https://github.com/KDABLabs/mecaps/blob/main/LICENSES/MIT.txt)
or the [Apache-2.0](https://github.com/KDABLabs/mecaps/blob/main/LICENSES/Apache-2.0.txt)
licenses.

Contact KDAB at <info@kdab.com> to inquire about additional features or
services related to this project.

## About KDAB

Mecaps is supported and maintained by Klarälvdalens Datakonsult AB (KDAB).

The KDAB Group is the global No.1 software consultancy for Qt, C++ and
OpenGL applications across desktop, embedded and mobile platforms.

The KDAB Group provides consulting and mentoring for developing Qt applications
from scratch and in porting from all popular and legacy frameworks to Qt.
We continue to help develop parts of Qt and are one of the major contributors
to the Qt Project. We can give advanced or standard trainings anywhere
around the globe on Qt as well as C++, OpenGL, 3D and more.

Please visit https://www.kdab.com to meet the people who write code like this.