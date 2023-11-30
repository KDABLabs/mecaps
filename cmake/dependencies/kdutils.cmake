set(KDUTILS_BUILD_TESTS OFF)

find_package(KDUtils CONFIG)

if(NOT KDUtils_FOUND)
    FetchContent_Declare(
        KDUtils
        GIT_REPOSITORY https://github.com/kdab/kdutils
        GIT_TAG 18183e5e3d3256781e0988950a7db04c89416f06 # v0.1.5
        USES_TERMINAL_DOWNLOAD YES
        USES_TERMINAL_UPDATE YES
    )

    option(KDUTILS_BUILD_TESTS "Build the tests" OFF)

    FetchContent_MakeAvailable(KDUtils)
endif()

find_package(KDFoundation CONFIG)
find_package(KDGui CONFIG)
