include(FetchContent)

include(cmake/dependencies/kdutils.cmake)

if (BUILD_INTEGRATION_CURL)
    include(cmake/dependencies/curl.cmake)
endif()

if (BUILD_INTEGRATION_KDGUI_SLINT)
    include(cmake/dependencies/slint.cmake)
endif()

if(BUILD_TESTS)
    include(cmake/dependencies/doctest.cmake)
    include(cmake/dependencies/fff.cmake)
endif()
