#include "cl_internal.h"

#include <stdlib.h>
#include <string.h>

static void cl_arena_track(cl_arena_t *arena, void *ptr) {
    if (arena->alloc_count == arena->alloc_capacity) {
        size_t new_capacity = arena->alloc_capacity ? arena->alloc_capacity * 2 : 16;
        void **new_allocations = malloc(new_capacity * sizeof(void *));
        if (!new_allocations) {
            abort();
        }
        if (arena->allocations) {
            memcpy(new_allocations, arena->allocations, arena->alloc_count * sizeof(void *));
            free(arena->allocations);
        }
        arena->allocations = new_allocations;
        arena->alloc_capacity = new_capacity;
    }
    arena->allocations[arena->alloc_count++] = ptr;
}

void *cl_arena_alloc_raw(cl_arena_t *arena, size_t size) {
    void *ptr = calloc(1, size);
    if (!ptr) {
        abort();
    }
    cl_arena_track(arena, ptr);
    return ptr;
}

char *cl_arena_strndup_raw(cl_arena_t *arena, const char *s, size_t n) {
    char *copy = cl_arena_alloc_raw(arena, n + 1);
    memcpy(copy, s, n);
    copy[n] = '\0';
    return copy;
}

char *cl_arena_strdup_raw(cl_arena_t *arena, const char *s) {
    return cl_arena_strndup_raw(arena, s, strlen(s));
}

void cl_array_grow_raw(cl_arena_t *arena, void **arr, size_t *count, size_t *capacity, size_t elem_size) {
    if (*count < *capacity) {
        return;
    }
    size_t new_capacity = *capacity ? *capacity * 2 : 4;
    void *new_arr = cl_arena_alloc_raw(arena, new_capacity * elem_size);
    if (*arr && *count) {
        memcpy(new_arr, *arr, (*count) * elem_size);
    }
    *arr = new_arr;
    *capacity = new_capacity;
}

void cl_arena_free_all(cl_arena_t *arena) {
    for (size_t i = 0; i < arena->alloc_count; i++) {
        free(arena->allocations[i]);
    }
    free(arena->allocations);
}

void *cl_arena_alloc(cl_document_t *doc, size_t size) {
    return cl_arena_alloc_raw(&doc->arena, size);
}

char *cl_arena_strdup(cl_document_t *doc, const char *s) {
    return cl_arena_strdup_raw(&doc->arena, s);
}

char *cl_arena_strndup(cl_document_t *doc, const char *s, size_t n) {
    return cl_arena_strndup_raw(&doc->arena, s, n);
}

void cl_array_grow(cl_document_t *doc, void **arr, size_t *count, size_t *capacity, size_t elem_size) {
    cl_array_grow_raw(&doc->arena, arr, count, capacity, elem_size);
}
