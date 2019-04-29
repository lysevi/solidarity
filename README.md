![logo](artwork/logo.small.png)
# SOLIDarity [![Build Status](https://travis-ci.org/lysevi/solidarity.svg?branch=master)](https://travis-ci.org/lysevi/solidarity)

# Dependencies
---
* Boost 1.69.0 or higher: system, asio, stacktrace, datetime.
* cmake 3.10 or higher
* conan.io 
* c++ 17 compiler (MSVC 2017, gcc 7.0)

## Building
---
1. add conan repo
```sh
$ conan remote add comunity https://api.bintray.com/conan/conan-community/conan 
```
2. build
```sh
$ git submodule update --init 
$ mkdir build
$ cd build
$ conan install ..
$ cmake ..
$ cmake --build . --config Release 
```

