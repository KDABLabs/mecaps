set(TARGET_NAME pdf)

add_library(${TARGET_NAME} STATIC pdf.cpp pdf_controller.cpp pdf_viewport.cpp)
add_library(mecaps::${TARGET_NAME} ALIAS ${TARGET_NAME})

target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(BUILD_INTEGRATION_PDF)
	set(PDFIUM_TARGET_NAME pdf_pdfium)
	add_library(${PDFIUM_TARGET_NAME} STATIC pdfium_backend.cpp)
	add_library(mecaps::${PDFIUM_TARGET_NAME} ALIAS ${PDFIUM_TARGET_NAME})
	target_include_directories(${PDFIUM_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
	target_link_libraries(${PDFIUM_TARGET_NAME} PUBLIC ${TARGET_NAME} PRIVATE pdfium)
endif()

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

	set(CONTROLLER_UNITTEST_TARGET_NAME test_pdf_controller)
	add_executable(${CONTROLLER_UNITTEST_TARGET_NAME} tst_pdf_controller.cpp)
	target_link_libraries(${CONTROLLER_UNITTEST_TARGET_NAME} PRIVATE ${TARGET_NAME} doctest::doctest)
	doctest_discover_tests(
		${CONTROLLER_UNITTEST_TARGET_NAME}
		ADD_LABELS
		1
		PROPERTIES
		LABELS
		"mecaps"
	)

	set(VIEWPORT_UNITTEST_TARGET_NAME test_pdf_viewport)
	add_executable(${VIEWPORT_UNITTEST_TARGET_NAME} tst_pdf_viewport.cpp)
	target_link_libraries(${VIEWPORT_UNITTEST_TARGET_NAME} PRIVATE ${TARGET_NAME} doctest::doctest)
	doctest_discover_tests(
		${VIEWPORT_UNITTEST_TARGET_NAME}
		ADD_LABELS
		1
		PROPERTIES
		LABELS
		"mecaps"
	)

	if(BUILD_INTEGRATION_PDF)
		set(PDFIUM_UNITTEST_TARGET_NAME test_pdf_pdfium)
		add_executable(${PDFIUM_UNITTEST_TARGET_NAME} tst_pdfium.cpp)
		target_compile_definitions(
			${PDFIUM_UNITTEST_TARGET_NAME}
			PRIVATE PDF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
		)
		target_link_libraries(${PDFIUM_UNITTEST_TARGET_NAME} PRIVATE ${PDFIUM_TARGET_NAME} doctest::doctest)
		doctest_discover_tests(
			${PDFIUM_UNITTEST_TARGET_NAME}
			ADD_LABELS
			1
			PROPERTIES
			LABELS
			"mecaps"
		)
	endif()
endif()
