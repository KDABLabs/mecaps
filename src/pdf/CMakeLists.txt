set(TARGET_NAME pdf)

add_library(${TARGET_NAME} STATIC pdf.cpp)
add_library(mecaps::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_link_libraries(${TARGET_NAME} PUBLIC pdfium)
