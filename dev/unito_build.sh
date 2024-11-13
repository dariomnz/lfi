#!/bin/bash
set -e
mkdir -p build
cd build

export LD_LIBRARY_PATH=/beegfs/home/javier.garciablas/dariomnz/bin/mpich/lib/:$LD_LIBRARY_PATH
export PATH=/beegfs/home/javier.garciablas/dariomnz/bin/mpich/bin/:$PATH

cmake .. -D BUILD_EXAMPLES=1 -D LIBFABRIC_PATH=/opt/libfabric -D MPI_PATH=/beegfs/home/javier.garciablas/dariomnz/bin/mpich

cmake --build . -j 8