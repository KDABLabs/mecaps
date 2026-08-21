set(SLINT_STYLE fluent CACHE STRING "The Slint widget style")
find_package(Slint 1.17.1 QUIET)

if(NOT Slint_FOUND)
  message(
    "Slint could not be located in the CMake module search path. Downloading it from Git and building it locally"
  )

  # set(SLINT_FEATURE_BACKEND_WINIT OFF) # this removes X11 support
  set(SLINT_FEATURE_BACKEND_QT OFF)
  set(SLINT_FEATURE_BACKEND_WINIT_WAYLAND ON)
  set(SLINT_FEATURE_RENDERER_SKIA ON)

  FetchContent_Declare(
    Slint
    GIT_REPOSITORY https://github.com/slint-ui/slint.git
    GIT_TAG v1.17.1
    SOURCE_SUBDIR api/cpp)
  FetchContent_MakeAvailable(Slint)
endif(NOT Slint_FOUND)
