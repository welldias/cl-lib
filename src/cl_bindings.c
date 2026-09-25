#include "cl_bindings.h"

#include <stdlib.h>
#include <string.h>

cl_bindings_t *cl_bindings_new(void) {
    cl_bindings_t *bindings = calloc(1, sizeof(cl_bindings_t));
    if (!bindings) {
        abort();
    }
    return bindings;
}

void cl_bindings_free(cl_bindings_t *bindings) {
    if (!bindings) {
        return;
    }
    cl_arena_free_all(&bindings->arena);
    free(bindings);
}

static cl_value_t *cl_bindings_new_value(cl_bindings_t *bindings, cl_value_kind_t kind) {
    if (!bindings) {
        return NULL;
    }
    cl_value_t *v = cl_arena_alloc_raw(&bindings->arena, sizeof(cl_value_t));
    v->kind = kind;
    return v;
}

cl_value_t *cl_bindings_string(cl_bindings_t *bindings, const char *value) {
    if (!value) {
        return NULL;
    }
    cl_value_t *v = cl_bindings_new_value(bindings, CL_VAL_STRING);
    if (v) {
        v->as.string_value = cl_arena_strdup_raw(&bindings->arena, value);
    }
    return v;
}

cl_value_t *cl_bindings_number(cl_bindings_t *bindings, double value) {
    cl_value_t *v = cl_bindings_new_value(bindings, CL_VAL_NUMBER);
    if (v) {
        v->as.number_value = value;
    }
    return v;
}

cl_value_t *cl_bindings_bool(cl_bindings_t *bindings, int value) {
    cl_value_t *v = cl_bindings_new_value(bindings, CL_VAL_BOOL);
    if (v) {
        v->as.bool_value = value ? 1 : 0;
    }
    return v;
}

cl_value_t *cl_bindings_null(cl_bindings_t *bindings) {
    return cl_bindings_new_value(bindings, CL_VAL_NULL);
}

cl_value_t *cl_bindings_list(cl_bindings_t *bindings) {
    return cl_bindings_new_value(bindings, CL_VAL_LIST); /* arena memory is zeroed: empty list */
}

cl_value_t *cl_bindings_object(cl_bindings_t *bindings) {
    return cl_bindings_new_value(bindings, CL_VAL_OBJECT);
}

/* True when `target` is `v` itself or appears anywhere inside it. Adding a
 * container into something it reaches would make the value cyclic, and the
 * deep copy made at evaluation time would never terminate. */
static int cl_value_reaches(const cl_value_t *v, const cl_value_t *target) {
    if (v == target) {
        return 1;
    }
    if (v->kind == CL_VAL_LIST) {
        for (size_t i = 0; i < v->as.list.count; i++) {
            if (cl_value_reaches(v->as.list.items[i], target)) {
                return 1;
            }
        }
    } else if (v->kind == CL_VAL_OBJECT) {
        for (size_t i = 0; i < v->as.object.count; i++) {
            if (cl_value_reaches(v->as.object.items[i].value, target)) {
                return 1;
            }
        }
    }
    return 0;
}

int cl_bindings_list_add(cl_bindings_t *bindings, cl_value_t *list, cl_value_t *item) {
    if (!bindings || !list || !item || list->kind != CL_VAL_LIST || cl_value_reaches(item, list)) {
        return -1;
    }
    cl_array_grow_raw(&bindings->arena, (void **)&list->as.list.items, &list->as.list.count,
                       &list->as.list.capacity, sizeof(cl_value_t *));
    list->as.list.items[list->as.list.count++] = item;
    return 0;
}

int cl_bindings_object_set(cl_bindings_t *bindings, cl_value_t *object, const char *key, cl_value_t *value) {
    if (!bindings || !object || !key || !value || object->kind != CL_VAL_OBJECT ||
        cl_value_reaches(value, object)) {
        return -1;
    }
    for (size_t i = 0; i < object->as.object.count; i++) {
        if (strcmp(object->as.object.items[i].key, key) == 0) {
            object->as.object.items[i].value = value;
            return 0;
        }
    }
    cl_array_grow_raw(&bindings->arena, (void **)&object->as.object.items, &object->as.object.count,
                       &object->as.object.capacity, sizeof(cl_value_object_item_t));
    cl_value_object_item_t *item = &object->as.object.items[object->as.object.count++];
    item->key = cl_arena_strdup_raw(&bindings->arena, key);
    item->value = value;
    return 0;
}

int cl_bindings_set(cl_bindings_t *bindings, const char *name, cl_value_t *value) {
    if (!bindings || !name || !value) {
        return -1;
    }
    for (size_t i = 0; i < bindings->count; i++) {
        if (strcmp(bindings->items[i].name, name) == 0) {
            bindings->items[i].value = value;
            return 0;
        }
    }
    cl_array_grow_raw(&bindings->arena, (void **)&bindings->items, &bindings->count, &bindings->capacity,
                       sizeof(cl_binding_t));
    cl_binding_t *binding = &bindings->items[bindings->count++];
    binding->name = cl_arena_strdup_raw(&bindings->arena, name);
    binding->value = value;
    return 0;
}

int cl_bindings_set_string(cl_bindings_t *bindings, const char *name, const char *value) {
    return cl_bindings_set(bindings, name, cl_bindings_string(bindings, value));
}

int cl_bindings_set_number(cl_bindings_t *bindings, const char *name, double value) {
    return cl_bindings_set(bindings, name, cl_bindings_number(bindings, value));
}

int cl_bindings_set_bool(cl_bindings_t *bindings, const char *name, int value) {
    return cl_bindings_set(bindings, name, cl_bindings_bool(bindings, value));
}

const cl_value_t *cl_bindings_lookup(const cl_bindings_t *bindings, const char *name) {
    if (!bindings) {
        return NULL;
    }
    for (size_t i = 0; i < bindings->count; i++) {
        if (strcmp(bindings->items[i].name, name) == 0) {
            return bindings->items[i].value;
        }
    }
    return NULL;
}
