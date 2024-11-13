#!/bin/bash
set -e
mkdir -p build
cd build

cmake .. -D BUILD_EXAMPLES=1 -D LIBFABRIC_PATH=$HOME/bin/libfabric -D MPI_PATH=$HOME/bin/mpich

cmake --build . -j 8