
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
extern "C"
{
#endif

    int lfi_server_create(const char *serv_addr, int *port);
    int lfi_server_accept(int id);
    int lfi_server_close(int id);

    int lfi_client_create(const char *serv_addr, int port);
    int lfi_client_close(int id);

    ssize_t lfi_send(int id, const void *data, size_t size);
    ssize_t lfi_recv(int id, void *data, size_t size);
    ssize_t lfi_any_recv(void *data, size_t size, int* out_source);

    ssize_t lfi_tsend(int id, const void *data, size_t size, int tag);
    ssize_t lfi_trecv(int id, void *data, size_t size, int tag);
    ssize_t lfi_any_trecv(void *data, size_t size, int tag, int* out_source);

    const char* lfi_strerror(int error);

#ifdef __cplusplus
}
#endif

#endif // _LFI_H