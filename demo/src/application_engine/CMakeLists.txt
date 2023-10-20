set(TARGET_NAME ${PROJECT_NAME}_application_engine)

add_library(${TARGET_NAME} STATIC application_engine.cpp)
add_library(mecaps::${TARGET_NAME} ALIAS ${TARGET_NAME})

slint_target_sources(${TARGET_NAME} ../../ui/app_window.slint)

target_link_libraries(${TARGET_NAME} PUBLIC mecaps::kdgui_slint_integration)

if (BUILD_INTEGRATION_CURL)
    target_link_libraries(${TARGET_NAME} PUBLIC mecaps::network_access_manager)
    target_compile_definitions(${TARGET_NAME} PRIVATE CURL_AVAILABLE)
endif()

if (BUILD_INTEGRATION_MQTT)
    target_link_libraries(${TARGET_NAME} PUBLIC mecaps::mqtt)
    target_compile_definitions(${TARGET_NAME} PRIVATE MOSQUITTO_AVAILABLE)
endif()
