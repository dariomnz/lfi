# include(ExternalProject)
# set(URING uring_project)
# ExternalProject_Add(
#     uring_project
#     GIT_REPOSITORY https://github.com/axboe/liburing.git
#     GIT_TAG liburing-2.9
#     PREFIX ${PROJECT_SOURCE_DIR}/external/${URING}
# 		CONFIGURE_COMMAND ${PROJECT_SOURCE_DIR}/external/${URING}/src/${URING}/configure --cc=gcc --cxx=g++
# 		BUILD_COMMAND make -j$(nproc)
#     INSTALL_COMMAND ""
#     UPDATE_COMMAND ""
# )
# add_library(uring STATIC IMPORTED)
# set_property(TARGET uring PROPERTY IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/lib/liburing.a)
# add_dependencies(uring uring_project)


set(URING_ROOT ${CMAKE_BINARY_DIR}/external/uring)
set(URING_LIB_DIR ${URING_ROOT}/bin/lib)
set(URING_INCLUDE_DIR ${URING_ROOT}/bin/include)

include(ExternalProject)

ExternalProject_Add(uring_external
                    PREFIX ${URING_ROOT}
                    GIT_REPOSITORY "https://github.com/axboe/liburing.git"
                    GIT_TAG "liburing-2.9"
                    UPDATE_COMMAND ""
                    PATCH_COMMAND ""
                    BINARY_DIR ${URING_ROOT}/src/uring
                    SOURCE_DIR ${URING_ROOT}/src/uring
                    INSTALL_DIR ${URING_ROOT}/bin
                    CONFIGURE_COMMAND ./configure --cc=gcc --cxx=g++ --prefix=${URING_ROOT}/bin
                    BUILD_COMMAND make -j$(nproc)
                    BUILD_BYPRODUCTS ${URING_LIB_DIR}/liburing.a)

add_library(liburing STATIC IMPORTED)
add_dependencies(liburing uring_external)

execute_process(
    COMMAND mkdir -p ${URING_INCLUDE_DIR}
    COMMENT "Create URING_INCLUDE_DIR"
)
set_target_properties(liburing PROPERTIES IMPORTED_LOCATION ${URING_LIB_DIR}/liburing.a
INTERFACE_INCLUDE_DIRECTORIES ${URING_INCLUDE_DIR})
