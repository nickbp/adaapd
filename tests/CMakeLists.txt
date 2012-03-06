cmake_minimum_required (VERSION 2.6)

project(tests)

include_directories(
	${gtest_INCLUDE_DIR}
	${ADAAPD_INCLUDES}
)

find_package(Threads)
set(gtest_libs ${gtest_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})

# TODO unit tests go here

#add_executable(test-x test-x.cpp)
#target_link_libraries(test-x adaapd ${gtest_libs})
#add_test(test-x test-x)