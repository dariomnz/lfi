#ifndef _COMMON_H
#define _COMMON_H

#include <stddef.h>
#include <stdint.h>
#include "lfi.h"

#define PORT 8082

enum op_type {
    OP_OPEN,
    OP_CLOSE,
    OP_RESIZE,
};

struct request {
    int op;
    char filename[256];
    size_t size;
};

struct response {
    uint64_t remote_addr;
    lfi_mr_key remote_key;
    size_t size;
    int status;
};

#endif
