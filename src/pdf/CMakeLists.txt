set(TARGET_NAME pdf)

add_library(${TARGET_NAME} STATIC pdf.cpp)
add_library(mecaps::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_TESTS)
	include(doctest)
	set(UNITTEST_TARGET_NAME test_${TARGET_NAME})
	add_executable(${UNITTEST_TARGET_NAME} tst_pdf.cpp)
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
