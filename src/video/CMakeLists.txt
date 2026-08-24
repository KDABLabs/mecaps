set(TARGET_NAME video)

add_library(${TARGET_NAME} STATIC
    video_player.cpp
    frame_mailbox.cpp
    gstreamer_video_player.cpp
)
add_library(mecaps::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_link_libraries(${TARGET_NAME}
    PUBLIC KDUtils::KDFoundation
    PRIVATE PkgConfig::GStreamer
    PRIVATE PkgConfig::GStreamerApp
    PRIVATE PkgConfig::GStreamerVideo
)

if(BUILD_TESTS)
    include(doctest)
    set(FRAME_MAILBOX_TEST_TARGET test_video_frame_mailbox)
    add_executable(${FRAME_MAILBOX_TEST_TARGET} tst_frame_mailbox.cpp)
    target_link_libraries(${FRAME_MAILBOX_TEST_TARGET} PRIVATE ${TARGET_NAME} doctest::doctest)
    doctest_discover_tests(
        ${FRAME_MAILBOX_TEST_TARGET}
        ADD_LABELS
        1
        PROPERTIES
        LABELS
        "mecaps"
    )
endif()