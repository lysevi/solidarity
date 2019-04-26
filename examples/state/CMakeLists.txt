MESSAGE(STATUS "\t inc")

FILE(GLOB HDRS "*.h")
FILE(GLOB SRC "*.cpp")

add_executable(ex_server_state server.cpp common.h)
TARGET_LINK_LIBRARIES(ex_server_state ${solidarity_LIBRARIES})
TARGET_LINK_DIRECTORIES(ex_server_state PUBLIC ${solidarity_LIBRARY_DIR})
target_include_directories(ex_server_state PUBLIC ${solidarity_INCLUDE_DIR})

add_executable(ex_client_state client.cpp common.h)
TARGET_LINK_LIBRARIES(ex_client_state ${solidarity_LIBRARIES})
TARGET_LINK_DIRECTORIES(ex_client_state PUBLIC ${solidarity_LIBRARY_DIR})
target_include_directories(ex_client_state PUBLIC ${solidarity_INCLUDE_DIR})

set_target_properties(ex_server_state PROPERTIES FOLDER examples)
set_target_properties(ex_client_state PROPERTIES FOLDER examples)
