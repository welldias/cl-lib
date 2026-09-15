#ifndef CL_INTERNAL_H
#define CL_INTERNAL_H

#include "cl/cl.h"

/* Every allocation made through these helpers is tracked on the arena and
 * released in one pass by cl_arena_free_all(). Growable arrays never use
 * realloc(): cl_array_grow_raw() allocates a fresh (larger) tracked block
 * and copies the old contents into it, leaving the old block tracked but
 * orphaned until the whole arena is freed. This keeps ownership simple and
 * avoids dangling pointers from a moved realloc block. Both cl_document_t
 * and cl_evaluated_t (see cl_eval.h) embed one of these. */
typedef struct cl_arena {
    void **allocations;
    size_t alloc_count;
    size_t alloc_capacity;
} cl_arena_t;

void *cl_arena_alloc_raw(cl_arena_t *arena, size_t size);
char *cl_arena_strdup_raw(cl_arena_t *arena, const char *s);
char *cl_arena_strndup_raw(cl_arena_t *arena, const char *s, size_t n);

/* Ensures room for one more element at (*arr)[*count], growing *arr and
 * *capacity first if they are equal. Does not touch *count; caller writes
 * the new element and increments *count itself. */
void cl_array_grow_raw(cl_arena_t *arena, void **arr, size_t *count, size_t *capacity, size_t elem_size);

/* Frees every tracked allocation plus the bookkeeping array itself. Does
 * not free `arena` itself (it is normally embedded in a larger struct). */
void cl_arena_free_all(cl_arena_t *arena);

struct cl_document {
    cl_body_t *root;
    char *filename;
    cl_arena_t arena;
};

/* Convenience wrappers bound to a document's own arena, used throughout
 * cl_ast.c/cl_mutate.c/cl_lexer.c/cl_parser.c/cl_template.c, which only
 * ever work with a cl_document_t. */
void *cl_arena_alloc(cl_document_t *doc, size_t size);
char *cl_arena_strdup(cl_document_t *doc, const char *s);
char *cl_arena_strndup(cl_document_t *doc, const char *s, size_t n);
void cl_array_grow(cl_document_t *doc, void **arr, size_t *count, size_t *capacity, size_t elem_size);

#endif /* CL_INTERNAL_H */
