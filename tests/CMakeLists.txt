cmake_minimum_required (VERSION 2.6)

project(tests)

include_directories(
	${gtest_INCLUDE_DIR}
	${ADAAPD_INCLUDES}
)

find_package(Threads)
set(gtest_libs ${gtest_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})

add_executable(test-tag test-tag.cc)
target_link_libraries(test-tag adaapd ${gtest_libs})
add_test(test-tag test-tag)

add_subdirectory(tagdata)