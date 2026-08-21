cmake_minimum_required(VERSION 3.21)

project(
    mecaps
    DESCRIPTION "MECAPS - a starter pack for Qt-free C++ (embedded) applications"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_DEMO_UI "Build Demo UI" ON)
option(BUILD_INTEGRATION_CURL "Build Curl Integration" ON)
option(BUILD_INTEGRATION_KDGUI_SLINT "Build KDGui/Slint Integration" ON)
option(BUILD_INTEGRATION_MQTT "Build MQTT Integration" ON) # this will be overriden in dependencies.cmake for now
option(BUILD_INTEGRATION_PDF "Build PDF Integration" OFF)
option(BUILD_TESTS "Build Tests" ON)

if (BUILD_INTEGRATION_PDF)
    if (NOT PDFium_DIR)
        message(FATAL_ERROR
            "BUILD_INTEGRATION_PDF requires PDFium_DIR to point to a provisioned PDFium SDK; "
            "mecaps does not download PDFium automatically."
        )
    endif()

    find_package(PDFium REQUIRED CONFIG)
    if (NOT TARGET pdfium)
        message(FATAL_ERROR
            "The PDFium package at '${PDFium_DIR}' does not provide the required 'pdfium' target."
        )
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/ECM/find-modules")

include(CTest)
include(cmake/dependencies.cmake)

set(CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE ON)

add_compile_definitions(
    $<$<CONFIG:Debug>:NDEBUG>
    SPDLOG_ACTIVE_LEVEL=$<IF:$<CONFIG:Debug>,SPDLOG_LEVEL_INFO,SPDLOG_LEVEL_WARN>
)

if (BUILD_INTEGRATION_CURL)
    add_subdirectory(src/network_access_manager)
endif()

if (BUILD_INTEGRATION_KDGUI_SLINT)
    add_subdirectory(src/kdgui_slint_integration)
endif()

if (BUILD_INTEGRATION_MQTT)
    add_subdirectory(src/mqtt)
endif()

if (BUILD_INTEGRATION_PDF)
    add_subdirectory(src/pdf)
endif()

if (BUILD_DEMO_UI AND BUILD_INTEGRATION_KDGUI_SLINT)
    add_subdirectory(demo)
endif()

if(BUILD_TESTS)
    enable_testing()
endif()
