set(TESTS_SRC
	test_never_null.cpp
	test_not_null.cpp
	test_pointer_traits.cpp
	test_shared_ptr.cpp
	test_wide_shared_ptr.cpp
	tests.cpp
)
add_executable(run-tests ${TESTS_SRC})
target_include_directories(run-tests
	PUBLIC ${PROJECT_SOURCE_DIR}
	PUBLIC ${PROJECT_SOURCE_DIR}/googletest/googletest/include
)
target_link_libraries(run-tests
	gtest
)
