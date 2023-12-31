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

if(BUILD_TESTS)
    include(doctest)
    set(UNITTEST_TARGET_NAME test_${TARGET_NAME})
    add_executable(${UNITTEST_TARGET_NAME} tst_network_access_manager.cpp tst_libcurl_stub.h)
    target_link_libraries(${UNITTEST_TARGET_NAME} PRIVATE ${TARGET_NAME} doctest::doctest)
    doctest_discover_tests(
        ${UNITTEST_TARGET_NAME}
        ADD_LABELS
        1
        PROPERTIES
        LABELS
        "mecaps"
    )
endif()

