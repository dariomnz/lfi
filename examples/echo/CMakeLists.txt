
find_library(MPI_LIBRARY NAMES mpi PATHS /usr/lib /usr/local/lib ${MPI_PATH}/lib)
find_path(MPI_INCLUDE_DIR mpi.h PATHS /usr/include /usr/local/include ${MPI_PATH}/include)

if(MPI_LIBRARY AND MPI_INCLUDE_DIR)
    include_directories(${MPI_INCLUDE_DIR})
    link_libraries(${MPI_LIBRARY})
else()
    message(FATAL_ERROR "MPI not found.  Make sure it is installed on your system. Or the MPI_PATH is set correctly")
endif()

set(EXAMPLES_BW
    echo_lfi_client_send
    echo_lfi_client_send_async
    echo_lfi_client_recv
    echo_lfi_server_async_tpool
    echo_lfi_server_async
    echo_lfi_server
    echo_mpi_client_send
    echo_mpi_client_send_async
    echo_mpi_client_recv
    echo_mpi_server_async
    echo_mpi_server
)

# add the executables cpp
foreach (file ${EXAMPLES_BW})
	add_executable(${file} ${file}.cpp)
    target_link_libraries(${file} lfi_static)
	install(TARGETS ${file} RUNTIME DESTINATION bin)
endforeach (file)