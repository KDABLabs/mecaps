set(TARGET_NAME network_access_manager)

add_library(${TARGET_NAME} STATIC
    abstract_transfer_handle.cpp
    http_transfer_handle.cpp
    ftp_transfer_handle.cpp
    network_access_manager.cpp
)
add_library(mecaps::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_link_libraries(${TARGET_NAME}
    PUBLIC KDUtils::KDFoundation
    PUBLIC CURL::libcurl
)
