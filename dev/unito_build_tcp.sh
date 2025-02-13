#!/bin/bash
set -e
mkdir -p build
cd build

export LD_LIBRARY_PATH=$HOME/dariomnz/bin/mpich-tcp/lib/:$HOME/dariomnz/bin/libfabric-2.0.0/lib/:$LD_LIBRARY_PATH
export PATH=$HOME/dariomnz/bin/mpich-tcp/bin/:$PATH

cmake .. -D BUILD_EXAMPLES=1 -D LIBFABRIC_PATH=$HOME/dariomnz/bin/libfabric-2.0.0 -D MPI_PATH=$HOME/dariomnz/bin/mpich-tcp

cmake --build . -j