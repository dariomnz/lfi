set(EXAMPLES_SIMPLE
    file_server
    file_client
)

# add the executables c
foreach (file ${EXAMPLES_SIMPLE})
	add_executable(${file} ${file}.c)
    set_target_properties(${file} PROPERTIES LINKER_LANGUAGE C)
	install(TARGETS ${file} RUNTIME DESTINATION bin)
endforeach (file)