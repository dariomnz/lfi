# Lightweight Fabric Interface (LFI)

**Lightweight Fabric Interface (LFI)** is a communication library designed to simplify access to high-performance, low-latency networks in High-Performance Computing (HPC) environments. Inspired by POSIX sockets, LFI provides an intuitive interface for communication through the Open Fabric Interface (OFI) library.

## Key Features

* **OFI Complexity Abstraction**: Simplifies the use of high-performance networks, making OFI more accessible.
* **Fault Tolerance**: Incorporates fault tolerance mechanisms for reliable communication base on heartbeats.
* **Point-to-Point Communication**: Facilitates connections between multiple processes.
* **Low Resource Consumption**: Minimizes resource usage, beneficial in multithreaded applications.
* **Tagged Message Protocol**: Enables precise message identification through tags.
* **Receive from Any Communicator**: Allows processes to wait for operations from multiple clients in a single thread.
* **Intuitive C API**: Provides an easy-to-use interface for C-compatible languages.

## Design and Architecture

LFI utilizes the Reliable Datagram Message (RDM) protocol over OFI, chosen for its wide implementation and message completion notification functionality. The library implements a dual-endpoint architecture to optimize intra-node and inter-node traffic, using shared memory for local communications and high-performance networks for remote communications.

## C API

LFI offers a C API structured in three header files: 
* **[lfi.h](include/lfi.h)**: for synchronous functions.
* **[lfi_async.h](include/lfi_async.h)**: for asynchronous functions.
* **[lfi_error.h](include/lfi_error.h)**: for error handling.

<details>
  <summary>Click to see the API</summary>

  For a more detailed explanation of the API, please refer to the corresponding header file.

  **[lfi.h](include/lfi.h)**
  ```c
  const char *lfi_strerror(int error);

  int lfi_server_create(const char *serv_addr, int *port);
  int lfi_server_accept(int id);
  int lfi_server_close(int id);

  int lfi_client_create(const char *serv_addr, int port);
  int lfi_client_close(int id);

  ssize_t lfi_send(int id, const void *data, size_t size);
  ssize_t lfi_tsend(int id, const void *data, size_t size, int tag);

  #define LFI_ANY_COMM_SHM  (0xFFFFFFFF - 1)
  #define LFI_ANY_COMM_PEER (0xFFFFFFFF - 2)

  ssize_t lfi_recv(int id, void *data, size_t size);
  ssize_t lfi_trecv(int id, void *data, size_t size, int tag);
  ```
  
  **[lfi_async.h](include/lfi_async.h)**
  ```c
  typedef struct lfi_request lfi_request;
  typedef void (*lfi_request_callback)(int error, void *context);

  lfi_request *lfi_request_create(int id);
  void lfi_request_free(lfi_request *request);

  bool lfi_request_completed(lfi_request *request);
  ssize_t lfi_request_size(lfi_request *request);
  ssize_t lfi_request_source(lfi_request *request);
  ssize_t lfi_request_error(lfi_request *request);
  void lfi_request_set_callback(lfi_request *request, lfi_request_callback func_ptr, void *context);

  ssize_t lfi_send_async(lfi_request *request, const void *data, size_t size);
  ssize_t lfi_tsend_async(lfi_request *request, const void *data, size_t size, int tag);

  ssize_t lfi_recv_async(lfi_request *request, void *data, size_t size);
  ssize_t lfi_trecv_async(lfi_request *request, void *data, size_t size, int tag);

  ssize_t lfi_wait(lfi_request *request);
  ssize_t lfi_wait_any(lfi_request *requests[], size_t size);
  ssize_t lfi_wait_all(lfi_request *requests[], size_t size);

  ssize_t lfi_cancel(lfi_request *request);
  ```
  **[lfi_error.h](include/lfi_error.h)**
  ```c
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
  ```
  </details>

## Usage Example
  Several examples of LFI usage can be found in the examples **[examples](examples)** folder.

<details>
  <summary>Here's a basic example of how to use LFI to send and receive messages:</summary>

  ```c
  // Server (server.c)
  #include "lfi.h"

  int main(void) {
    char buffer[1024];
    char* hello = "Hello from server";
    int port = 8080, s_id, c_id;
    if ((s_id = lfi_server_create(NULL, &port)) < 0){
      exit(EXIT_FAILURE);
    }
    if ((c_id = lfi_server_accept(s_id)) < 0){
      exit(EXIT_FAILURE);
    }
    ssize_t s, r;
    r = lfi_recv(c_id, buffer, 1024);
    s = lfi_send(c_id, hello, strlen(hello));
    lfi_client_close(c_id);
    lfi_server_close(s_id);
  }
  ```
  ```c
  // Client (client.c)
  #include "lfi.h"

  int main(int argc, char *argv[]) {
    char buffer[1024];
    char* hello = "Hello from client";
    int c_id=lfi_client_create(argv[1], 8080);
    if (c_id < 0) {
      return -1;
    }
    ssize_t s, r;
    s = lfi_send(c_id, hello, strlen(hello));
    r = lfi_recv(c_id, buffer, 1024);
    lfi_client_close(c_id);
  }
  ```
</details>

<details>
  <summary>Here's a basic example of how to use LFI to receive a message from any communicator:</summary>

  ```c
  #include "lfi_async.h"

  // Create the requests
  lfi_request shm_req = lfi_request_create(LFI_ANY_COMM_SHM);
  lfi_request peer_req = lfi_request_create(LFI_ANY_COMM_PEER);

  // Post the recv buffers
  const size_t size1 = 100, size2 = 100;
  char buffer1[size1], buffer2[size2];
  lfi_recv_async(shm_req, buffer1, size1);
  lfi_recv_async(peer_req, buffer2, size2);

  // Wait for one to complete
  lfi_request *requests[2] = {shm_req, peer_req};
  int completed = lfi_wait_any(requests, 2);
  if (completed == 0) {
      lfi_request_source(shm_req);
  } else if (completed == 1) {
      lfi_request_source(peer_req);
  } else {
      // Error
  }

  // Free the requests
  lfi_request_free(shm_req);
  lfi_request_free(peer_req);
  ```
</details>


## Requirements

### Base Library

* C++17
* CMake
* **[OFI (Open Fabric Interface)](https://github.com/ofiwg/libfabric)**

### Examples

* C++20
* CMake
* **[OFI (Open Fabric Interface)](https://github.com/ofiwg/libfabric)**
* **[MPI (tested with MPICH)](https://github.com/pmodels/mpich)**

## Getting Started
* **Clone the repository**:
```bash
git clone https://github.com/dariomnz/lfi.git
```
* **Compile the library**:
```bash
mkdir -p build
cd build

cmake .. -D BUILD_EXAMPLES=1 -D LIBFABRIC_PATH=<path to libfabric> -D MPI_PATH=<path to mpich>

cmake --build . -j $(nproc)
```
* **Run simple example**:
```bash
./build/examples/simple/simple_lfi_server &
sleep 1
./build/examples/simple/simple_lfi_client localhost
```