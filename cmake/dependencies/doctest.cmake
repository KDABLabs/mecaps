find_package(doctest QUIET)

if (NOT doctest_FOUND)
    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11
    )
    FetchContent_MakeAvailable(doctest)

    list(APPEND CMAKE_MODULE_PATH ${doctest_SOURCE_DIR}/scripts/cmake)

    target_include_directories(
        doctest
        INTERFACE $<BUILD_INTERFACE:${doctest_SOURCE_DIR}/doctest>
                  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/doctest>
    )

    if(APPLE)
        target_compile_options(doctest INTERFACE -Wno-deprecated-declarations)
    endif()

    message(${doctest_SOURCE_DIR})
    message(${CMAKE_INSTALL_INCLUDEDIR})
    install(
        DIRECTORY ${doctest_SOURCE_DIR}/doctest/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/doctest
    )
endif()

