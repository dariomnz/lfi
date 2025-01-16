#!/bin/bash
set -e
mkdir -p build
cd build

export LD_LIBRARY_PATH=$HOME/dariomnz/bin/mpich-ch4-fabric/lib/:$HOME/dariomnz/bin/libfabric-2.0.0/lib/:$LD_LIBRARY_PATH
export PATH=$HOME/dariomnz/bin/mpich-ch4-fabric/bin/:$PATH

cmake .. -D BUILD_EXAMPLES=1 -D LIBFABRIC_PATH=$HOME/dariomnz/bin/libfabric-2.0.0 -D MPI_PATH=$HOME/dariomnz/bin/mpich-ch4-fabric

cmake --build . -j