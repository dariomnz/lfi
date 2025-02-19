
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
#include <sys/types.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct lfi_request lfi_request;

    #define LFI_ANY_COMM_SHM  (0xFFFFFF - 1)
    #define LFI_ANY_COMM_PEER (0xFFFFFF - 2)

    lfi_request *lfi_request_create(int id);
    // It is only secure to free a request if is completed or canceled
    void lfi_request_free(lfi_request *request);
    bool lfi_request_completed(lfi_request *request);
    // When completed return values else -1
    ssize_t lfi_request_size(lfi_request *request);
    ssize_t lfi_request_source(lfi_request *request);
    ssize_t lfi_request_error(lfi_request *request);

    ssize_t lfi_send_async(lfi_request *request, const void *data, size_t size);
    ssize_t lfi_tsend_async(lfi_request *request, const void *data, size_t size, int tag);

    ssize_t lfi_recv_async(lfi_request *request, void *data, size_t size);
    ssize_t lfi_trecv_async(lfi_request *request, void *data, size_t size, int tag);

    ssize_t lfi_wait(lfi_request *request);
    // Return the index of the first request completed
    ssize_t lfi_wait_any(lfi_request *requests[], size_t size);
    ssize_t lfi_wait_all(lfi_request *requests[], size_t size);

    // When the request is cancelled it can be completed by the hardware, so, if it is important
    // for the user whether it is completed or not, it is necessary to check it after this call.
    ssize_t lfi_cancel(lfi_request *request);

#ifdef __cplusplus
}
#endif

#endif  // _LFI_ASYNC_H