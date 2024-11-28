#!/bin/bash
set -e
mkdir -p build
cd build

export LD_LIBRARY_PATH=$HOME/dariomnz/bin/mpich-ch4/lib/:$LD_LIBRARY_PATH
export PATH=$HOME/dariomnz/bin/mpich-ch4/bin/:$PATH

cmake .. -D BUILD_EXAMPLES=1 -D LIBFABRIC_PATH=/opt/libfabric -D MPI_PATH=$HOME/dariomnz/bin/mpich-ch4

cmake --build . -j 8