find_package(Slint QUIET)

if (NOT Slint_FOUND)
  message("Slint could not be located in the CMake module search path. Downloading it from Git and building it locally")

  set(SLINT_FEATURE_EXPERIMENTAL ON)
  # set(SLINT_FEATURE_BACKEND_WINIT OFF) # this removes X11 support
  set(SLINT_FEATURE_BACKEND_QT OFF)
  set(SLINT_FEATURE_BACKEND_WINIT_WAYLAND ON)
  set(SLINT_FEATURE_RENDERER_SKIA ON)
  set(SLINT_FEATURE_WAYLAND ON)

  FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    # `release/1` will auto-upgrade to the latest Slint >= 1.0.0 and < 2.0.0
    # `release/1.0` will auto-upgrade to the latest Slint >= 1.0.0 and < 1.1.0
    GIT_TAG 0984170cb3e2effb2c788238bdc42c53109966fa # v1.4.1
    SOURCE_SUBDIR api/cpp
  )
  FetchContent_MakeAvailable(Slint)
endif (NOT Slint_FOUND)
