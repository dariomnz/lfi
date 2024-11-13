
find_library(MPI_LIBRARY NAMES mpi PATHS /usr/lib /usr/local/lib ${MPI_PATH}/lib)
find_path(MPI_INCLUDE_DIR mpi.h PATHS /usr/include /usr/local/include ${MPI_PATH}/include)

if(MPI_LIBRARY AND MPI_INCLUDE_DIR)
    include_directories(${MPI_INCLUDE_DIR})
    link_libraries(${MPI_LIBRARY})
else()
    message(FATAL_ERROR "MPI not found.  Make sure it is installed on your system. Or the MPI_PATH is set correctly")
endif()

set(EXAMPLES_BW
    bw_socket_client
    bw_socket_server
    bw_lfi_client
    bw_lfi_server
)

# add the executables cpp
foreach (file ${EXAMPLES_BW})
	add_executable(${file} ${file}.cpp)
	install(TARGETS ${file} RUNTIME DESTINATION bin)
endforeach (file)