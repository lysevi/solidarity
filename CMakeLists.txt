cmake_minimum_required (VERSION 3.1)
set(PROJECT "solidarity")
project (${PROJECT} VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (WIN32)
	add_definitions(-D_WIN32_WINNT=0x0601)
	add_definitions(-D_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
	add_definitions(-D_SILENCE_CXX17_ALLOCATOR_VOID_DEPRECATION_WARNING)
endif ()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING
        "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
endif()

set(DOUBLE_CHECKS  OFF)
if (CMAKE_BUILD_TYPE EQUAL "DEBUG")
    message(STATUS "debug mode")
	set(DOUBLE_CHECKS  ON)
endif(CMAKE_BUILD_TYPE EQUAL "DEBUG")

MESSAGE( STATUS "CMAKE_BUILD_TYPE: " ${CMAKE_BUILD_TYPE} )

option(solidarity_ENABLE_TESTS "Enable testing of the solidarity." ON)
option(solidarity_ENABLE_EXAMPLES "Enable examples of the solidarity." ON)
option(solidarity_ENABLE_DOUBLECHECKS "More checks in code." ${DOUBLE_CHECKS})
option(solidarity_ENABLE_BENCHMARKS "Enable build benchmarks." ON)
option(solidarity_ASAN_UBSAN "clang asan" OFF)
option(solidarity_MSAN "clang msan" OFF)

if(solidarity_ASAN_UBSAN AND solidarity_MSAN)
  message(FATAL_ERROR "Sanitizers cannot be enabled simultaneously.")
endif()

MESSAGE(STATUS "Options: ")
MESSAGE(STATUS "solidarity_ENABLE_TESTS - " ${solidarity_ENABLE_TESTS})
MESSAGE(STATUS "solidarity_ENABLE_EXAMPLES - " ${solidarity_ENABLE_EXAMPLES})
MESSAGE(STATUS "solidarity_ENABLE_DOUBLECHECKS - " ${solidarity_ENABLE_DOUBLECHECKS})
MESSAGE(STATUS "solidarity_ENABLE_BENCHMARKS - " ${solidarity_ENABLE_BENCHMARKS})
MESSAGE(STATUS "solidarity_ASAN_UBSAN - " ${solidarity_ASAN_UBSAN})
MESSAGE(STATUS "solidarity_MSAN - " ${solidarity_MSAN})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cotire/CMake")

include(AddCXXCompilerFlag)
include(CompillerSettings)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
 macro(PRECOMPILED_HEADERS ARG)
 endmacro()
else()
 include(cotire)
 macro(PRECOMPILED_HEADERS ARG)
  cotire(${ARG})
 endmacro()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

########## BOOST
IF(NOT HEADER_ONLY)
    list(FIND CONAN_DEFINES_BOOST "-DBOOST_USE_STATIC_LIBS" IS_STATIC)
    if(${IS_STATIC} GREATER -1)
        set(Boost_USE_STATIC_LIBS ON)
    else()
        set(Boost_USE_STATIC_LIBS OFF)
	endif()
endif()

add_definitions(-DBOOST_ASIO_NO_DEPRECATED)
find_package(Boost 1.69.0 COMPONENTS system date_time REQUIRED)

IF(Boost_FOUND)
else()	
  MESSAGE(FATAL_ERROR "boost not found")
  EXIT()
ENDIF()
######### MSGPACK
add_definitions(-DMSGPACK_CXX17=ON)

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

SET(solidarity_PRIVATE_LIBRARIES  ${CMAKE_THREAD_LIBS_INIT} ${Boost_LIBRARIES})

add_subdirectory(solidarity)

if(solidarity_ENABLE_BENCHMARKS)
   add_definitions(-DENABLE_BENCHMARKS)
endif(solidarity_ENABLE_BENCHMARKS)

if (solidarity_ENABLE_TESTS)
  enable_testing()
  add_subdirectory(unittests)
endif()

if(${solidarity_ENABLE_EXAMPLES})
  SET(CXXOPTS_BUILD_TESTS OFF)
  SET(CXXOPTS_BUILD_EXAMPLES OFF)
  add_subdirectory(extern/cxxopts)
  include_directories(extern/cxxopts/include)
  add_subdirectory(examples)
endif(${solidarity_ENABLE_EXAMPLES})