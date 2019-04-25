MESSAGE(STATUS "\tnode_client")

FILE(GLOB HDRS "*.h")
FILE(GLOB SRC "*.cpp")

add_executable(bench_client_node main.cpp)
TARGET_LINK_LIBRARIES(bench_client_node ${solidarity_LIBRARIES})
TARGET_LINK_DIRECTORIES(bench_client_node PUBLIC ${solidarity_LIBRARY_DIR})
target_include_directories(bench_client_node PUBLIC ${solidarity_INCLUDE_DIR})

set_target_properties(bench_client_node PROPERTIES FOLDER perftest)

