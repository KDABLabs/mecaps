set(TARGET_NAME mqtt)

add_library(${TARGET_NAME} STATIC mqtt.cpp)
add_library(mecaps::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_link_libraries(${TARGET_NAME}
    PUBLIC KDUtils::KDFoundation
    PUBLIC PkgConfig::Mosquitto
)
