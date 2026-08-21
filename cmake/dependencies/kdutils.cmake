find_package(KDUtils CONFIG)

if(NOT KDUtils_FOUND)
    set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Build spdlog tests" FORCE)
    set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Build spdlog examples" FORCE)
    set(SPDLOG_INSTALL ON CACHE BOOL "Generate the spdlog package config" FORCE)
    set(SPDLOG_BUILD_PIC ON CACHE BOOL "Build position independent code" FORCE)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.17.0
    )
    FetchContent_MakeAvailable(spdlog)

    set(KDBindings_TESTS OFF CACHE BOOL "Build KDBindings tests" FORCE)
    set(KDBindings_EXAMPLES OFF CACHE BOOL "Build KDBindings examples" FORCE)
    FetchContent_Declare(
        KDBindings
        GIT_REPOSITORY https://github.com/KDAB/KDBindings.git
        GIT_TAG v1.1.0
    )
    FetchContent_MakeAvailable(KDBindings)

    set(GLM_BUILD_LIBRARY OFF CACHE BOOL "Build the GLM library" FORCE)
    set(GLM_BUILD_TESTS OFF CACHE BOOL "Build GLM tests" FORCE)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.3
    )
    FetchContent_MakeAvailable(glm)

    set(KDUTILS_USE_EXTERNAL_DEPENDENCIES ON CACHE BOOL "Use dependencies provided by Mecaps" FORCE)
    set(KDUTILS_BUILD_EXAMPLES OFF CACHE BOOL "Build KDUtils examples" FORCE)
    set(KDUTILS_BUILD_MQTT_SUPPORT OFF CACHE BOOL "Build KDMqtt" FORCE)
    set(KDUTILS_BUILD_TESTS OFF CACHE BOOL "Build KDUtils tests" FORCE)
    list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/find-modules")
    FetchContent_Declare(
        KDUtils
        GIT_REPOSITORY https://github.com/kdab/kdutils
        GIT_TAG ae7424f6445bb37a44428bb46dab8d645c62418a
        USES_TERMINAL_DOWNLOAD YES
        USES_TERMINAL_UPDATE YES
    )

    FetchContent_MakeAvailable(KDUtils)
    target_link_libraries(KDFoundation PUBLIC $<BUILD_INTERFACE:glm::glm-header-only>)
endif()

find_package(KDFoundation CONFIG)
find_package(KDGui CONFIG)
