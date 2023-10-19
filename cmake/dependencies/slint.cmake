find_package(Slint QUIET)

if (NOT Slint_FOUND)
  message("Slint could not be located in the CMake module search path. Downloading it from Git and building it locally")

  add_compile_definitions(SLINT_FEATURE_EXPERIMENTAL)
  set(SLINT_FEATURE_EXPERIMENTAL ON)
  # set(SLINT_FEATURE_BACKEND_WINIT OFF) # this removes X11 support
  set(SLINT_FEATURE_BACKEND_QT OFF)
  set(SLINT_FEATURE_BACKEND_WINIT_WAYLAND ON)
  set(SLINT_FEATURE_WAYLAND ON)
  set(SLINT_FEATURE_RENDERER_SKIA ON)

  include(FetchContent)

  FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    # `release/1` will auto-upgrade to the latest Slint >= 1.0.0 and < 2.0.0
    # `release/1.0` will auto-upgrade to the latest Slint >= 1.0.0 and < 1.1.0
    GIT_TAG dee1025e83c1e66e31fb192e0550d1c1c3f13012 # v1.2.1
    SOURCE_SUBDIR api/cpp
  )
  FetchContent_MakeAvailable(Slint)
endif (NOT Slint_FOUND)
