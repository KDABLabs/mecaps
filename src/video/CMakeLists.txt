set(TARGET_NAME video)

add_library(${TARGET_NAME} STATIC
    video_player.cpp
)
add_library(mecaps::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_link_libraries(${TARGET_NAME}
    PUBLIC KDUtils::KDFoundation
)