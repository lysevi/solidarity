![logo](artwork/logo.small.png)
# SOLIDarity [![Build Status](https://travis-ci.org/lysevi/solidarity.svg?branch=master)](https://travis-ci.org/lysevi/solidarity) [![Build status](https://ci.appveyor.com/api/projects/status/xir9ui0vtu9806aq/branch/master?svg=true)](https://ci.appveyor.com/project/lysevi/solidarity/branch/master) [![Coverage Status](https://coveralls.io/repos/github/lysevi/solidarity/badge.svg?branch=master)](https://coveralls.io/github/lysevi/solidarity?branch=master)

# Dependencies
---
* Boost 1.69.0 or higher: system, asio, stacktrace, datetime.
* cmake 3.10 or higher
* conan.io 
* c++ 17 compiler (MSVC 2017, gcc 7.0)

## Building
---
```sh
$ git submodule update --init 
$ mkdir build
$ cd build
$ conan install ..
$ cmake ..
$ cmake --build . --config Release 
```

