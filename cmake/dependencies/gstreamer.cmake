if (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "BUILD_INTEGRATION_VIDEO is supported only on Linux")
endif()

find_package(PkgConfig REQUIRED)

pkg_check_modules(GStreamer REQUIRED IMPORTED_TARGET "gstreamer-1.0>=1.20")
pkg_check_modules(GStreamerApp REQUIRED IMPORTED_TARGET "gstreamer-app-1.0>=1.20")
pkg_check_modules(GStreamerVideo REQUIRED IMPORTED_TARGET "gstreamer-video-1.0>=1.20")
