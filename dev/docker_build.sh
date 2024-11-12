
set -e
mkdir -p build
cd build

cmake .. -D BUILD_EXAMPLES=1 -D LIBFABRIC_PATH=/home/lab/bin/libfabric

cmake --build . -j 8