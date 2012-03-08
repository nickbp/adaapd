cmake_minimum_required (VERSION 2.6)

project(tests)

# zero-second files with some tag data:

configure_file (
	"${PROJECT_SOURCE_DIR}/empty.aiff"
	"${PROJECT_BINARY_DIR}/empty.aiff"
	COPYONLY
)

configure_file (
	"${PROJECT_SOURCE_DIR}/empty_gsm.wav"
	"${PROJECT_BINARY_DIR}/empty_gsm.wav"
	COPYONLY
)

configure_file (
	"${PROJECT_SOURCE_DIR}/empty.mp3"
	"${PROJECT_BINARY_DIR}/empty.mp3"
	COPYONLY
)

configure_file (
	"${PROJECT_SOURCE_DIR}/empty_ms16.wav"
	"${PROJECT_BINARY_DIR}/empty_ms16.wav"
	COPYONLY
)

configure_file (
	"${PROJECT_SOURCE_DIR}/empty.ogg"
	"${PROJECT_BINARY_DIR}/empty.ogg"
	COPYONLY
)

configure_file (
	"${PROJECT_SOURCE_DIR}/empty.wma"
	"${PROJECT_BINARY_DIR}/empty.wma"
	COPYONLY
)


# short-length files for formats where TagLib dislikes empty:

# won't open an empty.flac at all
configure_file (
	"${PROJECT_SOURCE_DIR}/short.flac"
	"${PROJECT_BINARY_DIR}/short.flac"
	COPYONLY
)

# works, but complains about no streams
configure_file (
	"${PROJECT_SOURCE_DIR}/short.m4a"
	"${PROJECT_BINARY_DIR}/short.m4a"
	COPYONLY
)
