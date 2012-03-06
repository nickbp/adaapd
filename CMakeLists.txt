cmake_minimum_required (VERSION 2.6)

set(adaapd_VERSION_MAJOR 1)
set(adaapd_VERSION_MINOR 0)
set(adaapd_VERSION_PATCH 0)

# INotify

find_path(inotify_INCLUDE_DIR NAMES sys/inotify.h)
if(inotify_INCLUDE_DIR)
	message(STATUS "Found INotify.")
else()
	message(ERROR "Didn't find INotify. Non-Linux systems are currently unsupported.")
endif()

# TagLib

find_path(taglib_INCLUDE_DIR NAMES taglib/taglib.h)
find_library(taglib_LIBRARY NAMES tag)
if(taglib_INCLUDE_DIR AND taglib_LIBRARY)
	message(STATUS "Found TagLib.")
	list(APPEND INCLUDES ${taglib_INCLUDE_DIR})
	list(APPEND LIBS ${taglib_LIBRARY})
else()
	message(ERROR "Didn't find TagLib. Install libtag1-dev and reconfigure.")
endif()

# SQLite

find_path(sqlite_INCLUDE_DIR NAMES sqlite3.h)
find_library(sqlite_LIBRARY NAMES sqlite3)
if(sqlite_INCLUDE_DIR AND sqlite_LIBRARY)
	message(STATUS "Found SQLite3.")
	list(APPEND INCLUDES ${sqlite_INCLUDE_DIR})
	list(APPEND LIBS ${sqlite_LIBRARY})
else()
	message(ERROR "Didn't find SQLite3. Install libsqlite3-dev and reconfigure.")
endif()

# YAML

find_path(yaml_INCLUDE_DIR NAMES yaml.h)
find_library(yaml_LIBRARY NAMES yaml)
if(yaml_INCLUDE_DIR AND yaml_LIBRARY)
	message(STATUS "Found YAML.")
	list(APPEND INCLUDES ${yaml_INCLUDE_DIR})
	list(APPEND LIBS ${yaml_LIBRARY})
else()
	message(ERROR "Didn't find YAML. Install libyaml-dev and reconfigure.")
endif()

# Ragel

find_program(ragel_EXE ragel)
if(ragel_EXE)
	message(STATUS "Found Ragel.")
	set(ragel_ARGS "-C -T0") #TODO try -T1, -Fn, -Gn ...
else()
	message(ERROR "Didn't find Ragel. Install 'ragel' and reconfigure.")
endif()

# GTest

find_path(gtest_INCLUDE_DIR NAMES gtest/gtest.h)
find_library(gtest_LIBRARY NAMES gtest)
if(gtest_INCLUDE_DIR AND gtest_LIBRARY)
	message(STATUS "Found GTest.")
	set(FOUND_GTEST ON)
else()
	message(STATUS "Didn't find GTest. Unit tests disabled.")
endif()

option(BUILD_TESTS "Build unit tests" ${FOUND_GTEST})

# Enable c++11

if(CMAKE_COMPILER_IS_GNUCXX)
	set(CMAKE_CXX_FLAGS "-std=c++0x -Wall")
endif()

# Paths

add_subdirectory(src)
if(BUILD_TESTS)
	set(ADAAPD_INCLUDES
		${PROJECT_SOURCE_DIR}/src
		${PROJECT_BINARY_DIR}/src
	)
	enable_testing()
	add_subdirectory(tests)
endif()

include (InstallRequiredSystemLibraries)
set (CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENCE")
set (CPACK_PACKAGE_VERSION_MAJOR "${adaapd_VERSION_MAJOR}")
set (CPACK_PACKAGE_VERSION_MINOR "${adaapd_VERSION_MINOR}")
set (CPACK_PACKAGE_VERSION_PATCH "${adaapd_VERSION_PATCH}")
include (CPack)