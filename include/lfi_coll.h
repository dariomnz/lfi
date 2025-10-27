
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

#ifndef _LFI_COLL_H
#define _LFI_COLL_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Represents an LFI group.
 *
 * This structure holds information about a group of processes or hosts
 * participating in a coordinated LFI operation.
 *
 * @note Currently, this implementation only supports one rank per host.
 * TODO: Future versions may extend this to support multiple ranks per host.
 *
 * @warning This structure should be treated as opaque by the user.
 * Its internal members should not be accessed directly. Use the provided API functions
 * to interact with `lfi_group` objects.
 */
typedef struct lfi_group {
    size_t size;
    int *ranks;
    int rank;
    double start_time;
} lfi_group;

/**
 * @brief Creates a new LFI group.
 *
 * This function initializes a new LFI group based on a list of hostnames.
 * An LFI group represents a collection of hosts that can participate in
 * coordinated operations.
 *
 * @param hostnames An array of strings, where each string is a hostname
 * belonging to the group.
 * @param n_hosts The number of hostnames in the `hostnames` array.
 * @param out_group A pointer to an `lfi_group` object where the newly created
 * group will be stored.
 * @return Returns 0 on success, or a non-zero error code if the group
 * could not be created.
 */
int lfi_group_create(const char *hostnames[], size_t n_hosts, lfi_group *out_group);

/**
 * @brief Retrieves the rank of the current process within an LFI group.
 *
 * The rank is a unique identifier for each process within a specific LFI group.
 *
 * @param group A pointer to the `lfi_group` object.
 * @param rank A pointer to an integer where the rank of the current process
 * within the group will be stored.
 * @return Returns 0 on success, or a non-zero error code if the rank
 * could not be retrieved.
 */
int lfi_group_rank(lfi_group *group, int *rank);

/**
 * @brief Retrieves the total size (number of processes/hosts) of an LFI group.
 *
 * The size represents the total number of participants in the LFI group.
 *
 * @param group A pointer to the `lfi_group` object.
 * @param size A pointer to an integer where the total size of the group
 * will be stored.
 * @return Returns 0 on success, or a non-zero error code if the size
 * could not be retrieved.
 */
int lfi_group_size(lfi_group *group, int *size);

/**
 * @brief Closes and releases resources associated with an LFI group.
 *
 * This function should be called when an LFI group is no longer needed
 * to free up any allocated resources.
 *
 * @param group A pointer to the `lfi_group` object to be closed.
 * @return Returns 0 on success, or a non-zero error code if the group
 * could not be closed properly.
 */
int lfi_group_close(lfi_group *group);

/**
 * @brief Retrieves the current time associated with an LFI group.
 *
 * This function can be used to get a synchronized or relevant time value
 * within the context of the LFI group's operations.
 *
 * @param group A pointer to the `lfi_group` object.
 * @return Returns a double representing the current time for the LFI group.
 */
double lfi_time(lfi_group *group);

/**
 * @brief Synchronizes all processes within a specified group.
 *
 * This function implements a barrier synchronization mechanism. It ensures that no process
 * proceeds beyond the barrier until all participating processes have reached the same point.
 *
 * @param group A pointer to the `lfi_group` object.
 * @return 0 on success, or a negative error code on failure.
 */
int lfi_barrier(lfi_group *group);

/**
 * @brief Broadcasts data from a root process to all other processes in a group.
 *
 * This function initiates a broadcast operation, where data residing on the root process
 * is sent to every other process within the specified communication group. This implementation
 * uses asynchronous send and receive operations.
 *
 * @param group A pointer to the `lfi_group` object.
 * @param root The rank of the root process that initiates and sends the broadcast data.
 * @param data A pointer to the buffer containing the data to be sent (if self is root),
 * or a pointer to the buffer where the received data will be stored (if self is not root).
 * @param size The size of the data buffer in bytes.
 * @return 0 on success, or a negative error code on failure.
 */
int lfi_broadcast(lfi_group *group, int root, void *data, size_t size);

enum lfi_op_enum {
    LFI_OP_MIN,
    LFI_OP_MAX,
    LFI_OP_SUM,
    LFI_OP_PROD,
};

enum lfi_op_type_enum {
    LFI_OP_TYPE_INT,
};

/**
 * @brief Broadcasts data from a root process to all other processes in a group.
 *
 * This function initiates a broadcast operation, where data residing on the root process
 * is sent to every other process within the specified communication group. This implementation
 * uses asynchronous send and receive operations.
 *
 * @param group A pointer to the `lfi_group` object.
 * @param root The rank of the root process that initiates and sends the broadcast data.
 * @param data A pointer to the buffer containing the data to be sent (if self is root),
 * or a pointer to the buffer where the received data will be stored (if self is not root).
 * @param size The size of the data buffer in bytes.
 * @return 0 on success, or a negative error code on failure.
 */
int lfi_allreduce(lfi_group *group, void *data, enum lfi_op_type_enum type, enum lfi_op_enum op);

#ifdef __cplusplus
}
#endif

#endif  // _LFI_COLL_H