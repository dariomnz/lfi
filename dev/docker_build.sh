#!/bin/bash
set -e
mkdir -p build
cd build

cmake .. -D CMAKE_EXPORT_COMPILE_COMMANDS=on -D BUILD_EXAMPLES=1 -D LIBFABRIC_PATH=$HOME/bin/libfabric -D CMAKE_C_COMPILER=clang -D CMAKE_CXX_COMPILER=clang++

cmake --build . -j 8