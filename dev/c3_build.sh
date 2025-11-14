#!/bin/bash
set -e
mkdir -p build
cd build

module load cmake mpich

MPI_PATH=/opt/ohpc/pub/mpi/mpich-4.3.0-ofi

export LD_LIBRARY_PATH=$MPI_PATH/lib/:$LD_LIBRARY_PATH

cmake .. -D BUILD_EXAMPLES=on \
         -D CMAKE_EXPORT_COMPILE_COMMANDS=1 \
         -D MPI_PATH=$MPI_PATH \
         -D BUILD_LD_PRELOAD=off \
         -D LIBFABRIC_LIBRARY=/home/tester005/dariomnz/bin/libfabric/lib/libfabric.so \
         -D LIBFABRIC_INCLUDE_DIR=/home/tester005/dariomnz/bin/libfabric/include

cmake --build . -j