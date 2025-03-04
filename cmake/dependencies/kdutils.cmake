set(KDUTILS_BUILD_TESTS OFF)

find_package(KDUtils CONFIG)

if(NOT KDUtils_FOUND)
    FetchContent_Declare(
        KDUtils
        GIT_REPOSITORY https://github.com/kdab/kdutils
        GIT_TAG 47a6a4316e25b582e04fe47e98951baa6a6cc5bb
        USES_TERMINAL_DOWNLOAD YES
        USES_TERMINAL_UPDATE YES
    )

    option(KDUTILS_BUILD_TESTS "Build the tests" OFF)

    FetchContent_MakeAvailable(KDUtils)
endif()

find_package(KDFoundation CONFIG)
find_package(KDGui CONFIG)
