set(EXAMPLES_SIMPLE
    simple_socket_client
    simple_socket_server
    simple_lfi_client
    simple_lfi_client_pp
    simple_lfi_client_async
    simple_lfi_server
    simple_lfi_server_pp
    simple_lfi_coll
    simple_lfi_allreduce
    simple_lfi_server_rma
    simple_lfi_client_rma
)

# add the executables c
foreach (file ${EXAMPLES_SIMPLE})
	add_executable(${file} ${file}.c)
    set_target_properties(${file} PROPERTIES LINKER_LANGUAGE C)
	install(TARGETS ${file} RUNTIME DESTINATION bin)
endforeach (file)

set(EXAMPLES_SIMPLE_CPP
    simple_lfi_server_any_comm
    simple_lfi_client_any_comm
)

# add the executables cpp
foreach (file ${EXAMPLES_SIMPLE_CPP})
	add_executable(${file} ${file}.cpp)
	install(TARGETS ${file} RUNTIME DESTINATION bin)
endforeach (file)