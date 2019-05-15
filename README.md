<p align="center"><img src="artwork/logo.small.png"></p>
<b>
<table>
    <tr>
        <td>
            master branch
        </td>
        <td>
            Linux <a href="https://travis-ci.org/lysevi/solidarity"><img src="https://travis-ci.org/lysevi/solidarity.svg?branch=master"></a>
        </td>
        <td>
            Windows <a href="https://ci.appveyor.com/project/lysevi/solidarity/branch/master"><img src="https://ci.appveyor.com/api/projects/status/xir9ui0vtu9806aq/branch/master?svg=true"></a>
        </td>
        <td>
            <a href="https://coveralls.io/github/lysevi/solidarity?branch=master"><img src="https://coveralls.io/repos/github/lysevi/solidarity/badge.svg?branch=master"></a>
        </td>
        <td>
            <a href="https://codecov.io/gh/lysevi/solidarity"><img src="https://codecov.io/gh/lysevi/solidarity/branch/master/graph/badge.svg"></a>
        </td>
    </tr>
    <tr>
        <td>
            dev branch
        </td>
        <td>
            Linux <a href="https://travis-ci.org/lysevi/solidarity"><img src="https://travis-ci.org/lysevi/solidarity.svg?branch=dev"></a>
        </td>
        <td>
            Windows <a href="https://ci.appveyor.com/project/lysevi/solidarity/branch/dev"><img src="https://ci.appveyor.com/api/projects/status/xir9ui0vtu9806aq/branch/dev?svg=true"></a>
        </td>
        <td>
            <a href="https://coveralls.io/github/lysevi/solidarity?branch=dev"><img src="https://coveralls.io/repos/github/lysevi/solidarity/badge.svg?branch=dev"></a>
        </td>
        <td>
            <a href="https://codecov.io/gh/lysevi/solidarity"><img src="https://codecov.io/gh/lysevi/solidarity/branch/master/graph/badge.svg"></a>
        </td>
    </tr>
</table>
</b>

# SOLIDarity 

## Dependencies
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

