find_package(CURL QUIET)
if (NOT TARGET CURL::libcurl)
    get_property(tmp GLOBAL PROPERTY PACKAGES_NOT_FOUND)
    list(FILTER tmp EXCLUDE REGEX CURL)
    set_property(GLOBAL PROPERTY PACKAGES_NOT_FOUND ${tmp})

    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG curl-8_8_0
    )
    set(BUILD_TESTING OFF CACHE BOOL "Turn off testing" FORCE)
    set(BUILD_CURL_EXE OFF CACHE BOOL "Turn off curl executable" FORCE)

    if (WIN32)
        set(CURL_USE_SCHANNEL ON CACHE BOOL "Use schannel to build libcurl" FORCE)
    else()
        set(CURL_USE_OPENSSL ON CACHE BOOL "Use OpenSSL to build libcurl" FORCE)
    endif()
    FetchContent_MakeAvailable(curl)
endif()
