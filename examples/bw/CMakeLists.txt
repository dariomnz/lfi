
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
    bw_lfi_client_async
    bw_lfi_server_async
    bw_lfi_any_comm_client
    bw_lfi_any_comm_server
    bw_mpi_client
    bw_mpi_server
)

# add the executables cpp
foreach (file ${EXAMPLES_BW})
	add_executable(${file} ${file}.cpp)
	install(TARGETS ${file} RUNTIME DESTINATION bin)
endforeach (file)

find_library(MERCURY_LIBRARY NAMES mercury PATHS /usr/lib /usr/local/lib ${MERCURY_PATH}/lib)
find_library(MERCURY_NA_LIBRARY NAMES na PATHS /usr/lib /usr/local/lib ${MERCURY_PATH}/lib)
find_path(MERCURY_INCLUDE_DIR mercury.h PATHS /usr/include /usr/local/include ${MERCURY_PATH}/include)

if(MERCURY_LIBRARY AND MERCURY_NA_LIBRARY AND MERCURY_INCLUDE_DIR)
    include_directories(${MERCURY_INCLUDE_DIR})
    link_libraries(${MERCURY_LIBRARY} ${MERCURY_NA_LIBRARY})
    message(STATUS "MERCURY_LIBRARY: ${MERCURY_LIBRARY}")
    message(STATUS "MERCURY_NA_LIBRARY: ${MERCURY_NA_LIBRARY}")
    message(STATUS "MERCURY_INCLUDE_DIR: ${MERCURY_INCLUDE_DIR}")
    
    set(EXAMPLES_BW_MERCURY
        bw_mercury_client
        bw_mercury_server
    )

    # add the executables cpp
    foreach (file ${EXAMPLES_BW_MERCURY})
        add_executable(${file} ${file}.cpp)
        install(TARGETS ${file} RUNTIME DESTINATION bin)
    endforeach (file)
else()
    message(STATUS "MERCURY not found. No mercury test will be built.")
endif()
