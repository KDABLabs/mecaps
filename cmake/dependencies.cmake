include(FetchContent)
include(cmake/dependencies/slint.cmake)
include(cmake/dependencies/spdlog.cmake)
include(cmake/dependencies/kdutils.cmake)
include(cmake/dependencies/curl.cmake)
include(cmake/dependencies/mosquitto.cmake)

# we need xcb symbols in order to get all the information necessary to create
# a slint window handle
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    find_package(
        XCB
        COMPONENTS XKB
        REQUIRED
    )
    list(APPEND LIBRARIES
        ${XCB_LIBRARIES}
        ${XKB_LIBRARIES}
    )
endif()
