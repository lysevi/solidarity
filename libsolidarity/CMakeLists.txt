set(PROJECT "solidarity")
project (${PROJECT} VERSION 0.0.1)

include(GenerateExportHeader)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/config.h.cmake ${CMAKE_CURRENT_SOURCE_DIR}/config.h)


FILE(GLOB GLOB_HDRS "*.h")
FILE(GLOB DIALLER_HDRS "dialler/*.h")
FILE(GLOB UTILS_HDRS "utils/*.h")
FILE(GLOB UTILS_ASYNC_HDRS "utils/async/*.h")

FILE(GLOB GLOB_SRC "*.cpp")
FILE(GLOB DIALLER_SRC "dialler/*.cpp")
FILE(GLOB UTILS_SRC "utils/*.cpp")
FILE(GLOB UTILS_ASYNC_SRC "utils/async/*.cpp")

source_group(\\ FILES ${GLOB_HDRS} ${GLOB_SRC})
source_group(dialler FILES ${DIALLER_HDRS} ${DIALLER_SRC})
source_group(utils FILES ${UTILS_HDRS} ${UTILS_SRC})
source_group(utils\\async FILES ${UTILS_ASYNC_HDRS} ${UTILS_ASYNC_SRC})

SET(SRC ${GLOB_HDRS} ${GLOB_SRC} 
    ${UTILS_HDRS}  ${UTILS_SRC} 
    ${DIALLER_HDRS} ${DIALLER_SRC}
	${UTILS_ASYNC_HDRS}  ${UTILS_ASYNC_SRC})


add_library(solidarity SHARED ${SRC})
TARGET_LINK_LIBRARIES(solidarity  ${solidarity_PRIVATE_LIBRARIES})
target_link_directories(solidarity PUBLIC ${solidarity_LIBRARY_DIR})
target_include_directories(solidarity PUBLIC ${solidarity_INCLUDE_DIR})
add_library(solidarity::solidarity ALIAS solidarity)

PRECOMPILED_HEADERS(solidarity)

IF(WIN32)
 SET(solidarity_NET_LIBRARIES  ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} wsock32 ws2_32 )
else()
 SET(solidarity_NET_LIBRARIES ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} )
endif()

SET(solidarity_LIBRARIES solidarity ${solidarity_PRIVATE_LIBRARIES} ${solidarity_NET_LIBRARIES} CACHE STRING "solidarity libs")


GENERATE_EXPORT_HEADER(solidarity
    BASE_NAME solidarity
    EXPORT_MACRO_NAME EXPORT
    EXPORT_FILE_NAME exports.h
    STATIC_DEFINE SHARED_EXPORTS_BUILT_AS_STATIC)
