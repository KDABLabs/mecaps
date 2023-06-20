cmake_minimum_required(VERSION 3.21)
project(my_application LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 20)

set(SOURCES
    src/main.cpp
    src/mecaps/window_adapter.cpp
    src/mecaps/slint_wrapper_window.cpp
    src/mecaps/mecaps.cpp
)

set(SLINT_SOURCES
    ui/appwindow.slint
)

set(LIBRARIES
    Slint::Slint
    spdlog::spdlog
    KDUtils::KDUtils
    KDUtils::KDFoundation
    KDUtils::KDGui
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/ECM/find-modules")


# configure dependencies

add_compile_definitions(SLINT_FEATURE_EXPERIMENTAL)
set(SLINT_FEATURE_EXPERIMENTAL ON)
# set(SLINT_FEATURE_BACKEND_WINIT OFF) # this removes X11 support
set(SLINT_FEATURE_BACKEND_QT OFF)
set(SLINT_FEATURE_BACKEND_WINIT_WAYLAND ON)
set(SLINT_FEATURE_WAYLAND ON)
set(SLINT_FEATURE_RENDERER_WINIT_SKIA ON)

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_compile_definitions(KD_PLATFORM_WAYLAND)
endif()

if (CMAKE_BUILD_TYPE MATCHES RELEASE)
    add_compile_definitions(NDEBUG)
else()
    # get all logs
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=0)
endif()

# get dependencies
include(cmake/dependencies.cmake)

add_executable(my_application ${SOURCES})

slint_target_sources(my_application ${SLINT_SOURCES})
target_link_libraries(my_application PRIVATE ${LIBRARIES})
include_directories(${spdlog_INCLUDE_DIRS} ${KDUtils_INCLUDE_DIRS})
target_include_directories(my_application PUBLIC $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>)
# On Windows, copy the Slint DLL next to the application binary so that it's found.
if (WIN32)
    add_custom_command(TARGET my_application POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:my_application> $<TARGET_FILE_DIR:my_application> COMMAND_EXPAND_LISTS)
endif()
