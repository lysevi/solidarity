MESSAGE(STATUS "Microbenchmarks")

FILE(GLOB HDRS "*.h")
FILE(GLOB SRC "*.cpp")

add_executable(microbenchmarks ${HDRS} ${SRC})
TARGET_LINK_LIBRARIES(microbenchmarks benchmark ${RFT_LIBRARIES})
TARGET_LINK_DIRECTORIES(microbenchmarks PUBLIC ${RFT_LIBRARY_DIR})
target_include_directories(microbenchmarks PUBLIC ${RFT_INCLUDE_DIR})

set_target_properties(microbenchmarks PROPERTIES FOLDER Benchmarks)
add_test(microbenchmarks ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/microbenchmarks) 
cotire(microbenchmarks)
