include(ExternalProject)

find_package(PkgConfig REQUIRED)
pkg_check_modules(Mosquitto IMPORTED_TARGET libmosquittopp REQUIRED)

file (DOWNLOAD https://test.mosquitto.org/ssl/mosquitto.org.crt mosquitto.org.crt)

#find_package(libmosquitto REQUIRED)

#if(NOT TARGET libmosquitto::libmosquitto)
#    get_property(tmp GLOBAL PROPERTY PACKAGES_NOT_FOUND)
#    list(
#        FILTER
#        tmp
#        EXCLUDE
#        REGEX
#        libmosquitto
#    )
#    set_property(GLOBAL PROPERTY PACKAGES_NOT_FOUND ${tmp})

#    if(UNIX AND NOT APPLE)
#        message(STATUS "mosquitto is built using make with existing Makefile on Linux")
#        ExternalProject_Add(
#            mosquitto
#            GIT_REPOSITORY      https://github.com/eclipse/mosquitto.git
#            GIT_TAG             3923526c6b4c048bbecad2506c4c9963bc46cd36 # v2.0.18
#            PREFIX              "mosquitto"

#            BUILD_IN_SOURCE     true
#            CONFIGURE_COMMAND   "" #nothing to configure
#            BUILD_COMMAND       make binary WITH_CJSON=no
#            INSTALL_COMMAND     "" #nothing to install
#        )
#    else()
#        message(STATUS "mosquitto is built using cmake on Windows and Mac")
#        ExternalProject_Add(
#            mosquitto
#            GIT_REPOSITORY      https://github.com/eclipse/mosquitto.git
#            GIT_TAG             3923526c6b4c048bbecad2506c4c9963bc46cd36 # v2.0.18
#            PREFIX              "mosquitto"
#        )
#    endif()
#endif()
