set(KDUTILS_BUILD_TESTS OFF)

find_package(KDUtils CONFIG)

if(NOT KDUtils_FOUND)
    FetchContent_Declare(
        KDUtils
        GIT_REPOSITORY https://github.com/kdab/kdutils
        GIT_TAG e43dd828633a64cf1aa3612b5ca961ce54b12bef # v0.1.4
        USES_TERMINAL_DOWNLOAD YES
        USES_TERMINAL_UPDATE YES
    )

    option(KDUTILS_BUILD_TESTS "Build the tests" OFF)

    FetchContent_MakeAvailable(KDUtils)
endif()

find_package(KDFoundation CONFIG)
find_package(KDGui CONFIG)
