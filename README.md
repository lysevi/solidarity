![logo](artwork/logo.small.png)
# SOLIDarity [![Build Status](https://travis-ci.com/lysevi/solidarity.svg?token=f5DQZyNQkzeWGPedHsM2&branch=master)](https://travis-ci.com/lysevi/solidarity)

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

