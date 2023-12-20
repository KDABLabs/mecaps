set(TARGET_NAME ${PROJECT_NAME}_application_engine)

add_library(${TARGET_NAME} STATIC application_engine.cpp)
add_library(mecaps::${TARGET_NAME} ALIAS ${TARGET_NAME})

slint_target_sources(${TARGET_NAME} ../../ui/app_window.slint)

target_link_libraries(${TARGET_NAME} PUBLIC mecaps::kdgui_slint_integration)

if (BUILD_INTEGRATION_CURL)
    target_link_libraries(${TARGET_NAME} PUBLIC mecaps::network_access_manager)
    target_compile_definitions(${TARGET_NAME} PUBLIC CURL_AVAILABLE)
endif()

if (BUILD_INTEGRATION_MQTT)
    target_link_libraries(${TARGET_NAME} PUBLIC mecaps::mqtt)
    target_compile_definitions(${TARGET_NAME} PUBLIC MOSQUITTO_AVAILABLE)
endif()

if(BUILD_TESTS)
    set(UNITTEST_TARGET_NAME test_${TARGET_NAME})
    add_executable(${UNITTEST_TARGET_NAME} tst_application_engine.cpp)
    target_include_directories(${UNITTEST_TARGET_NAME} PRIVATE
        ${CMAKE_BINARY_DIR}/_deps/kdutils-src/tests/auto/foundation/common
        ${CMAKE_BINARY_DIR}/_deps/slint-src/api/cpp/include
    )
    target_link_libraries(${UNITTEST_TARGET_NAME} PRIVATE ${TARGET_NAME} doctest::doctest)
endif()
