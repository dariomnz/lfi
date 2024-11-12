set(EXAMPLES_MULTI
    multi_socket_client
    multi_socket_server
    multi_lfi_client
    multi_lfi_server
)

# add the executables cpp
foreach (file ${EXAMPLES_MULTI})
	add_executable(${file} ${file}.c)
    set_target_properties(${file} PROPERTIES LINKER_LANGUAGE C)
	install(TARGETS ${file} RUNTIME DESTINATION bin)
endforeach (file)