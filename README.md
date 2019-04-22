![logo](artwork/logo.small.png)
# SOLIDarity

## building
1. add conan repo
```sh
$ conan remote add comunity https://api.bintray.com/conan/conan-community/conan 
```
2. build
```sh
$ mkdir build
$ cd build
$ conan install ..
$ cmake ..
$ cmake --build . --config Release 
```

