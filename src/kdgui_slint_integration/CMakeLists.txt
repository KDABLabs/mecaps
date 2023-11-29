set(TARGET_NAME kdgui_slint_integration)

add_library(${TARGET_NAME} STATIC
    kdgui_slint_integration.cpp
    slint_wrapper_window.cpp
    window_adapter.cpp
)
add_library(mecaps::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_link_libraries(${TARGET_NAME}
    PUBLIC KDUtils::KDGui
    PUBLIC Slint::Slint
)

# we need xcb symbols in order to get all the information necessary to create a slint window handle
if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    find_package(
        XCB
        COMPONENTS XKB
        REQUIRED
    )
    target_link_libraries(${TARGET_NAME}
        PRIVATE ${XCB_LIBRARIES}
        PRIVATE ${XKB_LIBRARIES}
    )
endif()

if(BUILD_TESTS)
    set(UNITTEST_TARGET_NAME test_${TARGET_NAME})
    add_executable(${UNITTEST_TARGET_NAME} tst_kdgui_slint_integration.cpp)
    target_link_libraries(${UNITTEST_TARGET_NAME} PRIVATE ${TARGET_NAME} doctest::doctest)
endif()
