cmake_minimum_required (VERSION 3.1)
set(PROJECT "utils")
project (${PROJECT} VERSION 0.0.1)

include(GenerateExportHeader)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/config.h.cmake ${CMAKE_CURRENT_SOURCE_DIR}/config.h)


FILE(GLOB GLOB_HDRS "*.h")
FILE(GLOB UTILS_ASYNC_HDRS "async/*.h")

FILE(GLOB GLOB_SRC "*.cpp")
FILE(GLOB UTILS_ASYNC_SRC "async/*.cpp")

source_group(\\ FILES ${GLOB_HDRS} ${GLOB_SRC})
source_group(async FILES ${UTILS_ASYNC_HDRS} ${UTILS_ASYNC_SRC})

SET(SRC ${GLOB_HDRS} ${GLOB_SRC} 
	${UTILS_ASYNC_HDRS}  ${UTILS_ASYNC_SRC})

add_library(utils ${SRC})
TARGET_LINK_LIBRARIES(utils ${solidarity_PRIVATE_LIBRARIES})
target_link_directories(utils PUBLIC ${solidarity_LIBRARY_DIR})
target_include_directories(utils PUBLIC ${solidarity_INCLUDE_DIR})
PRECOMPILED_HEADERS(utils)

GENERATE_EXPORT_HEADER(utils
    BASE_NAME utils
    EXPORT_MACRO_NAME EXPORT
    EXPORT_FILE_NAME utils_exports.h
    STATIC_DEFINE SHARED_EXPORTS_BUILT_AS_STATIC)
