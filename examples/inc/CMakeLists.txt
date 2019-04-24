MESSAGE(STATUS "\t inc")

FILE(GLOB HDRS "*.h")
FILE(GLOB SRC "*.cpp")

add_executable(server_inc server.cpp common.h)
TARGET_LINK_LIBRARIES(server_inc ${solidarity_LIBRARIES})
TARGET_LINK_DIRECTORIES(server_inc PUBLIC ${solidarity_LIBRARY_DIR})
target_include_directories(server_inc PUBLIC ${solidarity_INCLUDE_DIR})

add_executable(client_inc client.cpp common.h)
TARGET_LINK_LIBRARIES(client_inc ${solidarity_LIBRARIES})
TARGET_LINK_DIRECTORIES(client_inc PUBLIC ${solidarity_LIBRARY_DIR})
target_include_directories(client_inc PUBLIC ${solidarity_INCLUDE_DIR})

set_target_properties(server_inc PROPERTIES FOLDER examples)
PRECOMPILED_HEADERS(server_inc)
set_target_properties(client_inc PROPERTIES FOLDER examples)
PRECOMPILED_HEADERS(client_inc)
