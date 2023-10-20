###########################################################################
# TODO
# check here if mosquitto is present for now.
# override BUILD_INTEGRATION_MQTT accordingly.
# remove this as soon as we support build of libmosquittopp on all OSes
# and we are always able to build mqtt integration in case
# BUILD_INTEGRATION_MQTT is set to ON.
find_package(PkgConfig)

if (UNIX AND PKG_CONFIG_FOUND)
    pkg_check_modules(Mosquitto IMPORTED_TARGET libmosquittopp REQUIRED)
endif()

if (Mosquitto_FOUND AND BUILD_INTEGRATION_MQTT)
    set(BUILD_INTEGRATION_MQTT ON)
else ()
    set(BUILD_INTEGRATION_MQTT OFF)
endif()
###########################################################################

include(FetchContent)

include(cmake/dependencies/kdutils.cmake)

if (BUILD_INTEGRATION_CURL)
    include(cmake/dependencies/curl.cmake)
endif()

if (BUILD_INTEGRATION_MQTT)
    include(cmake/dependencies/mosquitto.cmake)
endif()

if (BUILD_INTEGRATION_KDGUI_SLINT)
    include(cmake/dependencies/slint.cmake)
endif()

if(BUILD_TESTS)
    include(cmake/dependencies/doctest.cmake)
endif()
