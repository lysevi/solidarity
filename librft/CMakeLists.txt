include(GenerateExportHeader)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/config.h.cmake ${CMAKE_CURRENT_SOURCE_DIR}/config.h)


FILE(GLOB GLOB_HDRS "*.h")
FILE(GLOB UTILS_HDRS "utils/*.h")
FILE(GLOB UTILS_ASYNC_HDRS "utils/async/*.h")

FILE(GLOB GLOB_SRC "*.cpp")
FILE(GLOB UTILS_SRC "utils/*.cpp")
FILE(GLOB UTILS_ASYNC_SRC "utils/async/*.cpp")

source_group(\\ FILES ${GLOB_HDRS} ${GLOB_SRC})
source_group(utils FILES ${UTILS_HDRS} ${UTILS_SRC})
source_group(utils\\async FILES ${UTILS_ASYNC_HDRS} ${UTILS_ASYNC_SRC})

SET(SRC ${GLOB_HDRS} ${GLOB_SRC} 
	${UTILS_HDRS}  ${UTILS_SRC} 
	${UTILS_ASYNC_HDRS}  ${UTILS_ASYNC_SRC})



add_library(rft ${SRC})
TARGET_LINK_LIBRARIES(rft ${YAAF_PRIVATE_LIBRARIES})
target_link_directories(rft PUBLIC ${YAAF_LIBRARY_DIR})
target_include_directories(rft PUBLIC ${YAAF_INCLUDE_DIR})
cotire(yrft)

SET(RFT_LIBRARIES rft ${RFT_PRIVATE_LIBRARIES} CACHE STRING "rft libs")

GENERATE_EXPORT_HEADER(rft
    BASE_NAME rft
    EXPORT_MACRO_NAME EXPORT
    EXPORT_FILE_NAME exports.h
    STATIC_DEFINE SHARED_EXPORTS_BUILT_AS_STATIC)
