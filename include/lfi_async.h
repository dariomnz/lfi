
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

#ifndef _LFI_ASYNC_H
#define _LFI_ASYNC_H

// #include <stdint.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Represents an LFI asynchronous request.
 */
typedef struct lfi_request lfi_request;

/**
 * @brief Callback function for LFI asynchronous requests.
 *
 * @param error An integer representing any error that occurred during the request. A value of 0 indicates success.
 * @param context A pointer to user-defined data that was provided when the callback was setted. This allows the
 * callback to access relevant state or information.
 */
typedef void (*lfi_request_callback)(int error, void *context);

/**
 * @brief Creates a new LFI asynchronous request.
 *
 * This function creates a new request object associated with the given client ID.
 *
 * @note It is valid to reuse the request when it has been completed or cancelled.
 *
 * @param id The ID of the client.
 * @return A pointer to the newly created 'lfi_request' object on success, or NULL on failure.
 */
lfi_request *lfi_request_create(int id);

/**
 * @brief Frees an LFI asynchronous request.
 *
 * This function frees the memory associated with the given request object.
 *
 * @note It is only safe to free a request if it has been completed or cancelled.
 *
 * @param request Pointer to the 'lfi_request' object to free.
 */
void lfi_request_free(lfi_request *request);

/**
 * @brief Checks if an LFI asynchronous request is completed.
 *
 * @param request Pointer to the 'lfi_request' object.
 * @return 'true' if the request is completed, 'false' otherwise.
 */
bool lfi_request_completed(lfi_request *request);

/**
 * @brief Returns the size of the data associated with a completed request.
 *
 * @param request Pointer to the 'lfi_request' object.
 * @return The size of the data on success (if the request is completed), or a negative error code on failure.
 */
ssize_t lfi_request_size(lfi_request *request);

/**
 * @brief Returns the source ID of the data associated with a completed request.
 *
 * @param request Pointer to the 'lfi_request' object.
 * @return The source of the data on success (if the request is completed), or -1 if the request is not completed or an
 * error occurred.
 */
ssize_t lfi_request_source(lfi_request *request);

/**
 * @brief Returns the error code associated with a request.
 *
 * @param request Pointer to the 'lfi_request' object.
 * @return 0 on success, or a negative error code on failure.
 */
ssize_t lfi_request_error(lfi_request *request);

/**
 * @brief Sets the callback function and context for an LFI asynchronous request.
 *
 * This function allows you to specify a callback function that will be invoked when the asynchronous LFI request
 * completes, along with a user-defined context pointer.
 *
 * @param request A pointer to the 'lfi_request' object for which the callback is being set.
 * @param func_ptr A function pointer to a 'lfi_request_callback' function. This function will be called when the
 * request completes.
 * @param context A pointer to user-defined data. This pointer will be passed back to the callback function.
 */
void lfi_request_set_callback(lfi_request *request, lfi_request_callback func_ptr, void *context);

/**
 * @brief Sends data asynchronously over the LFI connection.
 *
 * This function sends data asynchronously using the given request object.
 *
 * @param request Pointer to the 'lfi_request' object.
 * @param data Pointer to the data to send.
 * @param size The size of the data to send.
 * @return 0 on success, or a negative error code on failure.
 */
ssize_t lfi_send_async(lfi_request *request, const void *data, size_t size);

/**
 * @brief Sends tagged data asynchronously over the LFI connection.
 *
 * This function sends data with a tag asynchronously using the given request object.
 *
 * @param request Pointer to the 'lfi_request' object.
 * @param data Pointer to the data to send.
 * @param size The size of the data to send.
 * @param tag The tag associated with the data.
 * @return 0 on success, or a negative error code on failure.
 */
ssize_t lfi_tsend_async(lfi_request *request, const void *data, size_t size, int tag);

/**
 * @brief Receives data asynchronously over the LFI connection.
 *
 * This function receives data asynchronously using the given request object,
 * or from any actual client if the ANY_COMM constant is used when creating the request object.
 *
 * @note To use ANY_COMM, two calls to this function are required:
 *       - One with a request for LFI_ANY_COMM_SHM to receive data from shared memory.
 *       - Another with a request for LFI_ANY_COMM_PEER to receive data from the peer connection.
 *       Next a call to lfi_wait_any to wait for one msg.
 *
 * @param request Pointer to the 'lfi_request' object.
 * @param data Pointer to the buffer where the received data will be stored.
 * @param size The size of the buffer.
 * @return 0 on success, or a negative error code on failure.
 */
ssize_t lfi_recv_async(lfi_request *request, void *data, size_t size);

/**
 * @brief Receives tagged data asynchronously over the LFI connection.
 *
 * This function receives data with a specific tag asynchronously using the given request object,
 * or from any actual client if the ANY_COMM constant is used when creating the request object.
 *
 * @note To use ANY_COMM, two calls to this function are required:
 *       - One with a request for LFI_ANY_COMM_SHM to receive data from shared memory.
 *       - Another with a request for LFI_ANY_COMM_PEER to receive data from the peer connection.
 *       Next a call to lfi_wait_any to wait for one msg.
 *
 * @param request Pointer to the 'lfi_request' object.
 * @param data Pointer to the buffer where the received data will be stored.
 * @param size The size of the buffer.
 * @param tag The tag of the data to receive.
 * @return 0 on success, or a negative error code on failure.
 */
ssize_t lfi_trecv_async(lfi_request *request, void *data, size_t size, int tag);

typedef struct lfi_status {
    uint64_t size;
    uint32_t source;
    uint32_t tag;
    int32_t error;
} lfi_status;

int lfi_trecv_any(lfi_request *req_shm, void *buffer_shm, lfi_request *req_peer, void *buffer_peer, size_t size,
                  uint32_t tag, lfi_status *status);

/**
 * @brief Waits for an asynchronous request to complete.
 *
 * This function blocks until the given request is completed.
 *
 * @param request Pointer to the 'lfi_request' object.
 * @return 0 on success, or a negative error code on failure.
 */
ssize_t lfi_wait(lfi_request *request);

/**
 * @brief Waits for any of the given asynchronous requests to complete.
 *
 * This function blocks until at least one of the given requests is completed.
 *
 * @param requests Array of 'lfi_request' pointers.
 * @param size The number of requests in the array.
 * @return The index of the one completed request in the array, or a negative error code on failure.
 */
ssize_t lfi_wait_any(lfi_request *requests[], size_t size);

/**
 * @brief Waits for all of the given asynchronous requests to complete.
 *
 * This function blocks until all of the given requests are completed.
 *
 * @param requests Array of 'lfi_request' pointers.
 * @param size The number of requests in the array.
 * @return 0 on success, or a negative error code on failure.
 */
ssize_t lfi_wait_all(lfi_request *requests[], size_t size);

/**
 * @brief Cancels an asynchronous request.
 *
 * This function attempts to cancel the given request.
 *
 * @note When a request is canceled, it may still be completed by the hardware.
 *       If it is important for the user to know whether the request was ultimately completed,
 *       it is necessary to check it with 'lfi_request_completed()' after calling this function.
 *
 * @param request Pointer to the 'lfi_request' object to cancel.
 * @return 0 on success, or a negative error code on failure.
 */
ssize_t lfi_cancel(lfi_request *request);

#ifdef __cplusplus
}
#endif

#endif  // _LFI_ASYNC_H