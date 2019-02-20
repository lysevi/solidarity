MESSAGE(STATUS "Unittests")

FILE(GLOB HDRS "*.h")
FILE(GLOB SRC "*.cpp")

add_executable(rft_unittests ${HDRS} ${SRC})
TARGET_LINK_LIBRARIES(rft_unittests ${RFT_LIBRARIES})
TARGET_LINK_DIRECTORIES(rft_unittests PUBLIC ${RFT_LIBRARY_DIR})
target_include_directories(rft_unittests PUBLIC ${RFT_INCLUDE_DIR})

add_test(rft_unittests ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unittests) 
set_target_properties(rft_unittests PROPERTIES FOLDER Tests)
cotire(rft_unittests)
