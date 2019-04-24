MESSAGE(STATUS "\t counter")

FILE(GLOB HDRS "*.h")
FILE(GLOB SRC "*.cpp")

add_executable(ex_counter main.cpp)
TARGET_LINK_LIBRARIES(ex_counter ${solidarity_LIBRARIES})
TARGET_LINK_DIRECTORIES(ex_counter PUBLIC ${solidarity_LIBRARY_DIR})
target_include_directories(ex_counter PUBLIC ${solidarity_INCLUDE_DIR})

set_target_properties(ex_counter PROPERTIES FOLDER examples)
PRECOMPILED_HEADERS(ex_counter)
