cmake_minimum_required (VERSION 3.1)
set(PROJECT "solidarity")
project (${PROJECT} VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif()

MESSAGE( STATUS "CMAKE_BUILD_TYPE: " ${CMAKE_BUILD_TYPE} )

option(solidarity_ENABLE_TESTS "Enable testing of the yaaf." ON)
option(solidarity_ENABLE_DOUBLECHECKS "More checks in code." OFF)
option(solidarity_ENABLE_BENCHMARKS "Enable build benchmarks." ON)
option(solidarity_ASAN_UBSAN "clang asan" OFF)
option(solidarity_MSAN "clang msan" OFF)

if(solidarity_ASAN_UBSAN AND solidarity_MSAN)
  message(FATAL_ERROR "Sanitizers cannot be enabled simultaneously.")
endif()

MESSAGE(STATUS "Options: ")
MESSAGE(STATUS "solidarity_ENABLE_TESTS - " ${solidarity_ENABLE_TESTS})
MESSAGE(STATUS "solidarity_ENABLE_DOUBLECHECKS - " ${solidarity_ENABLE_DOUBLECHECKS})
MESSAGE(STATUS "solidarity_ENABLE_BENCHMARKS - " ${solidarity_ENABLE_BENCHMARKS})
MESSAGE(STATUS "solidarity_ASAN_UBSAN - " ${solidarity_ASAN_UBSAN})
MESSAGE(STATUS "solidarity_MSAN - " ${solidarity_MSAN})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cotire/CMake")

include(AddCXXCompilerFlag)
include(CompillerSettings)
include(cotire)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

IF(solidarity_ENABLE_DOUBLECHECKS)
 add_definitions(-DDOUBLE_CHECKS)
 if(MSVC)
   add_definitions(-D_ITERATOR_DEBUG_LEVEL=1)
 ENDIF(MSVC)
endif(solidarity_ENABLE_DOUBLECHECKS)

find_package (Threads)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(solidarity_ENABLE_TESTS)
   include_directories(extern/Catch2/single_include/catch2/)
endif(solidarity_ENABLE_TESTS)

SET(solidarity_LIBRARY_DIR)
SET(solidarity_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR} )
SET(solidarity_PRIVATE_LIBRARIES  ${CMAKE_THREAD_LIBS_INIT})	

add_subdirectory(libutils)
add_subdirectory(librft)

if(solidarity_ENABLE_BENCHMARKS)
   add_definitions(-DENABLE_BENCHMARKS)
endif(solidarity_ENABLE_BENCHMARKS)

if (solidarity_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(unittests)
endif()



