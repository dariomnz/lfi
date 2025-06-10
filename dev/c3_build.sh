#!/bin/bash
set -e
mkdir -p build
cd build

module load cmake mpich

MPI_PATH=/opt/ohpc/pub/mpi/mpich-4.3.0-ofi

export LD_LIBRARY_PATH=$MPI_PATH/lib/:$LD_LIBRARY_PATH

cmake .. -D BUILD_EXAMPLES=1 -D MPI_PATH=$MPI_PATH

cmake --build . -j