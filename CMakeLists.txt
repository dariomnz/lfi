
cmake_minimum_required(VERSION 3.16)

project(LFI VERSION 3.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options("-DDEBUG")
add_compile_options("-fPIC" "-O3" "-g" "-g3" "-ggdb" "-Wall" "-Wextra")

link_libraries("pthread" "dl")

# Libfabric include
find_library(LIBFABRIC_LIBRARY NAMES fabric PATHS /usr/lib /usr/local/lib ${LIBFABRIC_PATH}/lib)
find_path(LIBFABRIC_INCLUDE_DIR rdma/fabric.h PATHS /usr/include /usr/local/include ${LIBFABRIC_PATH}/include)

if(LIBFABRIC_LIBRARY AND LIBFABRIC_INCLUDE_DIR)
    include_directories(${LIBFABRIC_INCLUDE_DIR})
    link_libraries(${LIBFABRIC_LIBRARY})
else()
    message(FATAL_ERROR "libfabric not found.  Make sure it is installed on your system. Or the LIBFABRIC_PATH is set correctly")
endif()

add_subdirectory(src)

option(BUILD_EXAMPLES "Build the examples" OFF)
if(BUILD_EXAMPLES)
    message(STATUS "BUILD_EXAMPLES : true")
    add_subdirectory(examples)
else()
    message(STATUS "BUILD_EXAMPLES : false")
endif(BUILD_EXAMPLES)
unset(BUILD_EXAMPLES CACHE)
