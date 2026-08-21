cmake_minimum_required(VERSION 3.21)

project(pdfium_sdk_smoke LANGUAGES CXX)

enable_testing()
find_package(PDFium REQUIRED CONFIG)
if(NOT PDFium_VERSION VERSION_EQUAL "153.0.8009.0")
	message(FATAL_ERROR "Expected PDFium 153.0.8009.0, found ${PDFium_VERSION}")
endif()

add_executable(pdfium_sdk_smoke main.cpp)
target_link_libraries(pdfium_sdk_smoke PRIVATE pdfium)
target_compile_features(pdfium_sdk_smoke PRIVATE cxx_std_20)

add_test(NAME pdfium_sdk_smoke COMMAND pdfium_sdk_smoke)
