set(EXAMPLES_SIMPLE
    simple_socket_client
    simple_socket_server
    simple_lfi_client
    simple_lfi_server
)

# add the executables cpp
foreach (file ${EXAMPLES_SIMPLE})
	add_executable(${file} ${file}.c)
    set_target_properties(${file} PROPERTIES LINKER_LANGUAGE C)
	install(TARGETS ${file} RUNTIME DESTINATION bin)
endforeach (file)