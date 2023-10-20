cmake_minimum_required(VERSION 3.21)

project(
    mecaps_demo
    DESCRIPTION "MECAPS - a starter pack for Qt-free C++ (embedded) applications"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TARGET_NAME ${PROJECT_NAME})

set(SOURCES
    src/main.cpp
    src/mecaps/application_engine.cpp
    src/mecaps/abstract_transfer_handle.cpp
    src/mecaps/ftp_transfer_handle.cpp
    src/mecaps/http_transfer_handle.cpp
    src/mecaps/mecaps.cpp
    src/mecaps/network_access_manager.cpp
    src/mecaps/slint_wrapper_window.cpp
    src/mecaps/window_adapter.cpp
)

set(SLINT_SOURCES
    ui/app_window.slint
)

set(LIBRARIES
    Slint::Slint
    spdlog::spdlog
    KDUtils::KDUtils
    KDUtils::KDFoundation
    KDUtils::KDGui
    CURL::libcurl
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/ECM/find-modules")

if (CMAKE_BUILD_TYPE MATCHES RELEASE)
    add_compile_definitions(NDEBUG)
else()
    # get all logs
    add_compile_definitions(SPDLOG_ACTIVE_LEVEL=0)
endif()

# get dependencies
include(cmake/dependencies.cmake)

if(Mosquitto_FOUND)
    list(APPEND LIBRARIES PkgConfig::Mosquitto)
    list(APPEND SOURCES src/mecaps/mqtt.cpp)
endif()

add_executable(${TARGET_NAME} ${SOURCES})

if(Mosquitto_FOUND)
    target_compile_definitions(${TARGET_NAME} PRIVATE MOSQUITTO_AVAILABLE)
endif()

slint_target_sources(${TARGET_NAME} ${SLINT_SOURCES})
target_link_libraries(${TARGET_NAME} PRIVATE ${LIBRARIES})
include_directories(${spdlog_INCLUDE_DIRS} ${KDUtils_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})
target_include_directories(${TARGET_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>)
# On Windows, copy the Slint DLL next to the application binary so that it's found.
if (WIN32)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:${TARGET_NAME}> $<TARGET_FILE_DIR:${TARGET_NAME}> COMMAND_EXPAND_LISTS)
    target_compile_definitions(${TARGET_NAME} PRIVATE NOMINMAX)
endif()
