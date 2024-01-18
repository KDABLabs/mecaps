set(TARGET_NAME mqtt)

add_library(${TARGET_NAME} STATIC mqtt.cpp)
add_library(mecaps::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_link_libraries(${TARGET_NAME}
    PUBLIC KDUtils::KDFoundation
    PUBLIC PkgConfig::Mosquitto
)

if(BUILD_TESTS)
    include(doctest)
    set(UNITTEST_TARGET_NAME test_${TARGET_NAME})
    add_executable(${UNITTEST_TARGET_NAME} tst_mqtt.cpp)
    target_include_directories(${UNITTEST_TARGET_NAME} PRIVATE ${CMAKE_BINARY_DIR}/_deps/kdutils-src/tests/auto/foundation/common)
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
