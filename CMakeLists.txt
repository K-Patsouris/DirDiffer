cmake_minimum_required(VERSION 3.10)

project(
	DirDiffer
	VERSION 1.0.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCE_DIR "${PROJECT_SOURCE_DIR}/src")
set(SOURCE_FILES
	"${SOURCE_DIR}/configuration.cpp"
	"${SOURCE_DIR}/configuration.h"
	"${SOURCE_DIR}/differ.cpp"
	"${SOURCE_DIR}/differ.h"
	"${SOURCE_DIR}/dynamic_buffer.h"
	"${SOURCE_DIR}/file.cpp"
	"${SOURCE_DIR}/file.h"
	"${SOURCE_DIR}/filesystem_interface.cpp"
	"${SOURCE_DIR}/filesystem_interface.h"
	"${SOURCE_DIR}/int_defs.h"
	"${SOURCE_DIR}/logger.cpp"
	"${SOURCE_DIR}/logger.h"
	"${SOURCE_DIR}/lowercase_path.cpp"
	"${SOURCE_DIR}/lowercase_path.h"
	"${SOURCE_DIR}/main.cpp"
	"${SOURCE_DIR}/rng.h"
	"${SOURCE_DIR}/sample_config.h"
	"${SOURCE_DIR}/serialization.cpp"
	"${SOURCE_DIR}/serialization.h"
	"${SOURCE_DIR}/smtp.cpp"
	"${SOURCE_DIR}/smtp.h"
	"${SOURCE_DIR}/string_defs.h"
	"${SOURCE_DIR}/string_utils.cpp"
	"${SOURCE_DIR}/string_utils.h"
	"${SOURCE_DIR}/winapi_funcs.cpp"
	"${SOURCE_DIR}/winapi_funcs.h"
)

add_executable(DirDiffer ${SOURCE_FILES})

target_compile_features("${PROJECT_NAME}" PRIVATE cxx_std_23)

find_package(CURL CONFIG REQUIRED)
target_link_libraries(DirDiffer PRIVATE CURL::libcurl)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
	target_compile_options(
		"${PROJECT_NAME}"
		PRIVATE
			"/std:c17"	# Force some relevant C for potential .c files
			
			"/sdl"	# Enable Additional Security Checks
			"/utf-8"	# Set Source and Executable character sets to UTF-8
			"/Zi"	# Debug Information Format

			"/permissive-"	# Standards conformance
			"/Zc:preprocessor"	# Enable preprocessor conformance mode

			"/wd4200" # nonstandard extension used : zero-sized array in struct/union

			"$<$<CONFIG:DEBUG>:>"
			"$<$<CONFIG:RELEASE>:/Zc:inline;/JMC-;/O2;/Ob2>;/Oi;/Ot;/GL"
	)

	target_link_options(
		"${PROJECT_NAME}"
		PRIVATE
			"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
			"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>;/LTCG:incremental"
	)
endif()


