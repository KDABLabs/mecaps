set(KDUTILS_BUILD_TESTS OFF)

find_package(KDUtils CONFIG)

if(NOT KDUtils_FOUND)
    FetchContent_Declare(
        KDUtils
        GIT_REPOSITORY https://github.com/kdab/kdutils
        GIT_TAG 8d383cff0f2925ffdb39d60b9ccd2a61139915ac # v0.1.2
        USES_TERMINAL_DOWNLOAD YES USES_TERMINAL_UPDATE YES
    )

    option(KDUTILS_BUILD_TESTS "Build the tests" OFF)

    FetchContent_MakeAvailable(KDUtils)
endif()

find_package(KDFoundation CONFIG)
find_package(KDGui CONFIG)
