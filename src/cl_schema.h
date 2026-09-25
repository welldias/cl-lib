#ifndef CL_SCHEMA_H
#define CL_SCHEMA_H

#include "cl_internal.h"

typedef struct cl_schema_attr {
    char *name;
    cl_schema_type_t type; /* always CL_TYPE_STRING for an enum */
    int required;
    char **values;      /* enum: the allowed strings; NULL otherwise */
    size_t value_count;
} cl_schema_attr_t;

/* Rules are allocated one by one and kept in arrays of POINTERS, so a
 * cl_schema_block_t* handed out to the host stays valid when an array
 * grows (cl_array_grow_raw moves the array itself, not what it points to). */
struct cl_schema_block {
    char *type; /* NULL for the schema's root level */
    cl_schema_attr_t **attrs;
    size_t attr_count;
    size_t attr_capacity;
    cl_schema_block_t **blocks;
    size_t block_count;
    size_t block_capacity;
    cl_schema_t *owner;
};

struct cl_schema {
    cl_schema_block_t root; /* only its `blocks` are used: the top-level rules */
    int strict;
    cl_arena_t arena;
};

#endif /* CL_SCHEMA_H */
