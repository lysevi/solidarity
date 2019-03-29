include(GenerateExportHeader)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/config.h.cmake ${CMAKE_CURRENT_SOURCE_DIR}/config.h)

FILE(GLOB GLOB_HDRS "*.h")
FILE(GLOB GLOB_SRC "*.cpp")

source_group(\\ FILES ${GLOB_HDRS} ${GLOB_SRC})

SET(SRC ${GLOB_HDRS} ${GLOB_SRC})

add_library(dialler  ${SRC})
target_INCLUDE_DIRECTORIES(dialler PUBLIC ${solidarity_INCLUDE_DIR} ${DIALLER_INCLUDE_DIR} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/lib)
target_link_libraries(dialler PUBLIC ${DIALLER_PRIVATE_LIBRARIES})
target_link_directories(dialler PUBLIC ${DIALLER_LIBRARY_DIR})
add_library(solidarity::dialler ALIAS dialler)

SET(DIALLER_LIBRARIES solidarity::dialler ${DIALLER_PRIVATE_LIBRARIES} CACHE STRING "dialler libs")

GENERATE_EXPORT_HEADER(dialler
    BASE_NAME dialler
    EXPORT_MACRO_NAME EXPORT
    EXPORT_FILE_NAME exports.h
    STATIC_DEFINE SHARED_EXPORTS_BUILT_AS_STATIC)
