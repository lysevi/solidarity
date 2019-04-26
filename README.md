![logo](artwork/logo.small.png)
# SOLIDarity [![Build Status](https://travis-ci.org/lysevi/solidarity.svg?branch=master)](https://travis-ci.org/lysevi/solidarity)

## building
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

