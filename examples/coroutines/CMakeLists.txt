
set(EXAMPLES_CO
    co_lfi_client
    co_lfi_server
    co_cat_lfi_client
    co_cat_lfi_server
    co_cat_uring_lfi_server
    liburing_op_support
    test_cat
)

include(FindLibUring.cmake)

# add the executables cpp
foreach (file ${EXAMPLES_CO})
	add_executable(${file} ${file}.cpp)
    set_property(TARGET ${file} PROPERTY CXX_STANDARD 20)
    target_link_libraries(${file} rt liburing)
	install(TARGETS ${file} RUNTIME DESTINATION bin)
endforeach (file)