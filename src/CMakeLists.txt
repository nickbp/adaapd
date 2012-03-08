cmake_minimum_required (VERSION 2.6)

project(adaapd)

configure_file (
  "${PROJECT_SOURCE_DIR}/version.in.h"
  "${PROJECT_BINARY_DIR}/version.h"
)

include_directories(
	${PROJECT_BINARY_DIR} # for version.h
	${inotify_INCLUDE_DIR}
	${taglib_INCLUDE_DIR}
	${sqlite_INCLUDE_DIR}
	${yaml_INCLUDE_DIR}
)

add_custom_command (
	OUTPUT ${PROJECT_BINARY_DIR}/daap-sm.c
	COMMAND ${ragel_EXE} ${ragel_ARGS} -o ${PROJECT_BINARY_DIR}/daap-sm.c daap-sm.rl
	DEPENDS daap-sm.rl # automatically rebuild
)

add_library(adaapd STATIC
  #config.cc
  #listener.cc
  logging.cc
  main.cc
  #playlist.cc
  tag.cc
  #yaml.cc
)
target_link_libraries(adaapd
	${taglib_LIBRARY}
	${sqlite_LIBRARY}
	${yaml_LIBRARY}
)

add_executable(adaapd_exe
	main.cc
)
target_link_libraries(adaapd_exe
	adaapd
)
# produce executable named 'adaapd', while avoiding collision with 'adaapd' lib above:
set_target_properties(adaapd_exe
	PROPERTIES OUTPUT_NAME adaapd
)