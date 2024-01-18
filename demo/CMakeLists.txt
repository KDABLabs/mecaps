set(TARGET_NAME ${PROJECT_NAME}_demo_ui)

add_subdirectory(src/application_engine)

add_executable(${TARGET_NAME} src/main.cpp)

target_link_libraries(${TARGET_NAME} PRIVATE ${PROJECT_NAME}_application_engine)

# Deployment: Copy the mosquitto.org.cert file next to the application binary so that it's found.
if(BUILD_INTEGRATION_MQTT)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/mosquitto.org.crt"
        $<TARGET_FILE_DIR:${TARGET_NAME}>
    )
endif()

if (WIN32)
    target_compile_definitions(${TARGET_NAME} PRIVATE NOMINMAX)
    # Deployment: On Windows, copy the Slint DLL next to the application binary so that it's found.
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
        $<TARGET_RUNTIME_DLLS:${TARGET_NAME}>
        $<TARGET_FILE_DIR:${TARGET_NAME}> COMMAND_EXPAND_LISTS
    )
endif()
