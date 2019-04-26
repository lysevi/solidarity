MESSAGE(STATUS "Unittests")

FILE(GLOB HDRS "*.h")
FILE(GLOB SRC "*.cpp")

add_executable(solidarity_unittests ${HDRS} ${SRC})
TARGET_LINK_LIBRARIES(solidarity_unittests ${solidarity_LIBRARIES} ${CMAKE_DL_LIBS})
TARGET_LINK_DIRECTORIES(solidarity_unittests PUBLIC ${solidarity_LIBRARY_DIR})
target_include_directories(solidarity_unittests PUBLIC ${solidarity_INCLUDE_DIR})

add_test(NAME solidarity_unittests COMMAND solidarity_unittests) 
set_target_properties(solidarity_unittests PROPERTIES FOLDER Tests)
PRECOMPILED_HEADERS(solidarity_unittests)
