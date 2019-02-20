cmake_minimum_required (VERSION 3.1)
set(PROJECT "rft")
project (${PROJECT} VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

IF(NOT CMAKE_BUILD_TYPE)
    message(STATUS "CMAKE_BUILD_TYPE is not set, set to default = RELWITHDEBINFO")
    SET(CMAKE_BUILD_TYPE "RELWITHDEBINFO")
ENDIF()

MESSAGE( STATUS "CMAKE_BUILD_TYPE: " ${CMAKE_BUILD_TYPE} )

option(rft_ENABLE_TESTS "Enable testing of the yaaf." ON)
option(rft_ENABLE_DOUBLECHECKS "More checks in code." OFF)
option(rft_ENABLE_BENCHMARKS "Enable build benchmarks." ON)
option(rft_ASAN_UBSAN "clang asan" OFF)
option(rft_MSAN "clang msan" OFF)

if(rft_ASAN_UBSAN AND rft_MSAN)
  message(FATAL_ERROR "Sanitizers cannot be enabled simultaneously.")
endif()

MESSAGE(STATUS "Options: ")
MESSAGE(STATUS "rft_ENABLE_TESTS - " ${rft_ENABLE_TESTS})
MESSAGE(STATUS "rft_ENABLE_DOUBLECHECKS - " ${rft_ENABLE_DOUBLECHECKS})
MESSAGE(STATUS "rft_ENABLE_BENCHMARKS - " ${rft_ENABLE_BENCHMARKS})
MESSAGE(STATUS "rft_ASAN_UBSAN - " ${rft_ASAN_UBSAN})
MESSAGE(STATUS "rft_MSAN - " ${rft_MSAN})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cotire/CMake")

include(AddCXXCompilerFlag)
include(CompillerSettings)
include(cotire)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

IF(rft_ENABLE_DOUBLECHECKS)
 add_definitions(-DDOUBLE_CHECKS)
ENDIF()

find_package (Threads)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(rft_ENABLE_TESTS)
   include_directories(extern/Catch2/single_include/catch2/)
endif(rft_ENABLE_TESTS)

SET(rft_LIBRARY_DIR)
SET(rft_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR} )
SET(rft_PRIVATE_LIBRARIES  ${CMAKE_THREAD_LIBS_INIT})	

add_subdirectory(librft)

if (rft_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(unittests)
endif()

if(rft_ENABLE_BENCHMARKS)
   set(BENCHMARK_ENABLE_TESTING OFF)
   add_subdirectory(extern/benchmark)
   include_directories(extern/benchmark/include)
   set_target_properties(benchmark PROPERTIES FOLDER Extern)
   add_subdirectory (microbenchmarks)
endif(rft_ENABLE_BENCHMARKS)

