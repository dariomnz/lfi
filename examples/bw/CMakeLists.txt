find_package(MPI REQUIRED)

set(EXAMPLES_BW
    bw_socket_client
    bw_socket_server
    bw_lfi_client
    bw_lfi_server
)

# add the executables cpp
foreach (file ${EXAMPLES_BW})
	add_executable(${file} ${file}.cpp)
    target_link_libraries(${file} MPI::MPI_C)
	install(TARGETS ${file} RUNTIME DESTINATION bin)
endforeach (file)