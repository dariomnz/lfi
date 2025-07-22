
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

#ifndef _LFI_ERROR_H
#define _LFI_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

#define LFI_SUCCESS         0   // Success
#define LFI_ERROR           1   // General error
#define LFI_TIMEOUT         2   // Timeout
#define LFI_CANCELED        3   // Canceled
#define LFI_BROKEN_COMM     4   // Broken comunicator
#define LFI_COMM_NOT_FOUND  5   // Comunicator not found
#define LFI_PEEK_NO_MSG     6   // No msg encounter
#define LFI_NOT_COMPLETED   7   // Request not completed
#define LFI_NULL_REQUEST    8   // Request is NULL
#define LFI_SEND_ANY_COMM   9   // Use of ANY_COMM in send
#define LFI_LIBFABRIC_ERROR 10  // Internal libfabric error
#define LFI_GROUP_NO_INIT   11  // The group is not initialized
#define LFI_GROUP_NO_SELF   12  // The hostname of the current process is missing
#define LFI_GROUP_INVAL     13  // Invalid argument

#ifdef __cplusplus
}
#endif

#endif  // _LFI_ERROR_H