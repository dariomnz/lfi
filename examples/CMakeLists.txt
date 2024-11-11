
add_executable(socket_client socket_client.c)
add_executable(socket_server socket_server.c)
add_executable(lfi_client lfi_client.c)
add_executable(lfi_server lfi_server.c)

target_link_libraries(lfi_client lfi)
target_link_libraries(lfi_server lfi)