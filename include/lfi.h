
/*
 *  Copyright 2024-2025 Dario Muñoz Muñoz, Felix Garcia Carballeira, Diego Camarmas Alonso, Alejandro Calderon Mateos
 *
 *  This file is part of LFI.
 *
 *  LFI is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  LFI is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with LFI.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _LFI_H
#define _LFI_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns a string describing the error code. 
 *
 * This function returns a human-readable string describing the given LFI error code.
 * Similar to libc strerror.
 *
 * @param error The LFI error code.
 * @return A pointer to a string describing the error.
 */
const char *lfi_strerror(int error);

/**
 * @brief Creates a new LFI server.
 *
 * This function creates a server and binds it to the specified address.
 *
 * @param serv_addr The server address (IP address), it can be NULL in order to not bind to a specific IP.
 * @param port Pointer to an integer where the allocated port number will be stored.  If 0 is provided, a free port will
 * be automatically assigned.
 * @return A non-negative integer representing the server ID on success, or a negative error code on failure.
 */
int lfi_server_create(const char *serv_addr, int *port);

/**
 * @brief Accepts a connection from a client.
 *
 * This function accepts a connection request on the server associated with the given ID.
 *
 * @param id The server ID.
 * @return A non-negative integer representing the client ID on success, or a negative error code on failure.
 */
int lfi_server_accept(int id);

/**
 * @brief Accepts a connection from a client with a timeout.
 *
 * This function accepts a connection request on the server associated with the given ID.
 *
 * @param id The server ID.
 * @param timeout_ms The maximum time in milliseconds to wait for a connection.
 * @return A non-negative integer representing the client ID on success, or a negative error code on failure.
 */
int lfi_server_accept_t(int id, int timeout_ms);

/**
* @brief Closes a server.
*
* This function closes the server associated with the given ID.
*
* @param id The ID of the server to close.
* @return 0 on success, or a negative error code on failure.
*/
int lfi_server_close(int id);

/**
 * @brief Creates a new LFI client and connects to a server.
 *
 * This function creates a client and connects it to the server at the specified address and port.
 *
 * @param serv_addr The server address (IP address or hostname).
 * @param port The server port.
 * @return A non-negative integer representing the client ID on success, or a negative error code on failure.
 */
int lfi_client_create(const char *serv_addr, int port);

/**
 * @brief Creates a new LFI client and connects to a server with a timeout.
 *
 * This function creates a client and connects it to the server at the specified address and port.
 *
 * @param serv_addr The server address (IP address or hostname).
 * @param port The server port.
 * @param timeout_ms The maximum time in milliseconds to wait for the connection to be established.
 * @return A non-negative integer representing the client ID on success, or a negative error code on failure.
 */
int lfi_client_create_t(const char *serv_addr, int port, int timeout_ms);

/**
* @brief Closes a client.
*
* This function closes the client associated with the given client ID.
*
* @param id The ID of the client to close.
* @return 0 on success, or a negative error code on failure.
*/
int lfi_client_close(int id);

/**
 * @brief Sends data over the LFI client.
 *
 * This function sends data over the client associated with the given ID.
 *
 * @param id The ID of the client.
 * @param data Pointer to the data to send.
 * @param size The size of the data to send.
 * @return The number of bytes sent on success, or a negative error code on failure.
 */
ssize_t lfi_send(int id, const void *data, size_t size);

/**
 * @brief Sends tagged data over the LFI client.
 *
 * This function sends data with a tag over the client associated with the given ID.
 * Tags can be used to distinguish different types of messages.
 *
 * @param id The ID of the client.
 * @param data Pointer to the data to send.
 * @param size The size of the data to send.
 * @param tag The tag associated with the data.
 * @return The number of bytes sent on success, or a negative error code on failure.
 */
ssize_t lfi_tsend(int id, const void *data, size_t size, int tag);

/**
 * @brief Constant representing the ID of any client over shared memory to recv data.
 */
#define LFI_ANY_COMM_SHM  (0xFFFFFFFF - 1)

/**
 * @brief Constant representing the ID of any client over a peer connection to recv data.
 */
#define LFI_ANY_COMM_PEER (0xFFFFFFFF - 2)

/**
 * @brief Receives data over the LFI connection.
 *
 * This function receives data over the client associated with the given ID, 
 * or from any actual client if the ANY_COMM constant is used.
 *
 * @note To use ANY_COMM, two calls to this function are required, each in its own thread:
 *       - One with id = LFI_ANY_COMM_SHM to receive data from shared memory.
 *       - Another with id = LFI_ANY_COMM_PEER to receive data from the peer connection.
 *       For single-threaded operation, consider using the asynchronous API.
 *
 * @param id The ID of the client, or ANY_COMM constant.
 * @param data Pointer to the buffer where the received data will be stored.
 * @param size The size of the buffer.
 * @return The number of bytes received on success, or a negative error code on failure.
 */
ssize_t lfi_recv(int id, void *data, size_t size);

/**
 * @brief Receives tagged data over the LFI connection.
 *
 * This function receives data with a specific tag over the client associated with the given ID, 
 * or from any actual client if the ANY_COMM constant is used.
 * 
 * @note To use ANY_COMM, two calls to this function are required, each in its own thread:
 *       - One with id = LFI_ANY_COMM_SHM to receive data from shared memory.
 *       - Another with id = LFI_ANY_COMM_PEER to receive data from the peer connection.
 *       For single-threaded operation, consider using the asynchronous API.
 *
 * @param id The ID of the client, or ANY_COMM constant.
 * @param data Pointer to the buffer where the received data will be stored.
 * @param size The size of the buffer.
 * @param tag The tag of the data to receive.
 * @return The number of bytes received on success, or a negative error code on failure.
 */
ssize_t lfi_trecv(int id, void *data, size_t size, int tag);

#ifdef __cplusplus
}
#endif

#endif  // _LFI_H