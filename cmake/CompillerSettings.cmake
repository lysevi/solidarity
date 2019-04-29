if(__compiler_settings_flag)
  return()
endif()
set(__compiler_settings_flag INCLUDED)

IF(WIN32)
  add_definitions(-DNOMINMAX)
else(WIN32)
  MESSAGE(STATUS "UNIX")
  add_definitions(-DUNIX_OS)

  IF(ENABLE_DOUBLECHECKS)
    MESSAGE(STATUS "Enable cc:rdynamic")
    add_cxx_compiler_flag(-rdynamic)
  ENDIF()
ENDIF(WIN32)

if(MSVC)
  add_definitions(-DMSVC)
  add_definitions(-D_ENABLE_ATOMIC_ALIGNMENT_FIX)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_WIN32_WINNT=0x0501 /permissive /WX -W4 /MP")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Ox /GT /Ot -DNDEBUG")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Oy") #-fno-omit-frame-pointer analog
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -D_DEBUG")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /sdl")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO  "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} /sdl")
endif(MSVC)

if(CMAKE_COMPILER_IS_GNUCXX)
  MESSAGE(STATUS "gcc compiller")
  add_definitions(-DGNU_CPP) 
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pipe ")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftemplate-backtrace-limit=0")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror -pedantic-errors -Wno-pedantic")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -fno-inline -g3 -fstack-protector-all -DDEBUG")
  set(CMAKE_CXX_FLAGS_RELEASE "-Ofast -g0 -march=native -mtune=native -DNDEBUG")

  #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-strict-aliasing -Werror=pragmas") #boost-shared-mutex error;
endif(CMAKE_COMPILER_IS_GNUCXX)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  MESSAGE(STATUS "clang compiller")
  add_definitions(-DCLANG_CPP)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -Wall")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Weverything -Werror")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=deprecated-declarations -Wno-error=deprecated")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-c++98-compat -Wno-c++98-compat-pedantic")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-return-stack-address -Wno-undef")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-variable -Wno-unused-parameter")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-weak-vtables -Wno-padded")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-global-constructors -Wno-exit-time-destructors")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-shorten-64-to-32 -Wno-sign-conversion")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-missing-prototypes -Wno-missing-variable-declarations")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-shadow -Wno-old-style-cast")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-documentation -Wno-documentation-unknown-command")

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-cast-align -Wno-disabled-macro-expansion -Wno-newline-eof -Wno-header-hygiene")
endif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
