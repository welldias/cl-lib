#include "cl_eval.h"

#include "cl_bindings.h"
#include "cl_functions.h"
#include "cl_writer.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Error reporting + value construction (result-arena backed)           */
/* ------------------------------------------------------------------ */

void cl_eval_fail(cl_eval_ctx_t *ctx, int line, int col, const char *fmt, ...) {
    if (ctx->failed) {
        return; /* keep the first error, same policy as the parser */
    }
    ctx->failed = 1;
    if (ctx->err) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(ctx->err->message, sizeof(ctx->err->message), fmt, ap);
        va_end(ap);
        ctx->err->line = line;
        ctx->err->col = col;
    }
}

static cl_value_t *cl_val_new(cl_eval_ctx_t *ctx, cl_value_kind_t kind) {
    cl_value_t *v = cl_arena_alloc_raw(&ctx->result->arena, sizeof(cl_value_t));
    v->kind = kind;
    return v;
}

cl_value_t *cl_val_string(cl_eval_ctx_t *ctx, const char *s) {
    cl_value_t *v = cl_val_new(ctx, CL_VAL_STRING);
    v->as.string_value = cl_arena_strdup_raw(&ctx->result->arena, s);
    return v;
}

cl_value_t *cl_val_string_n(cl_eval_ctx_t *ctx, const char *s, size_t n) {
    cl_value_t *v = cl_val_new(ctx, CL_VAL_STRING);
    v->as.string_value = cl_arena_alloc_raw(&ctx->result->arena, n + 1);
    memcpy(v->as.string_value, s, n);
    v->as.string_value[n] = '\0';
    return v;
}

cl_value_t *cl_val_number(cl_eval_ctx_t *ctx, double n) {
    cl_value_t *v = cl_val_new(ctx, CL_VAL_NUMBER);
    v->as.number_value = n;
    return v;
}

cl_value_t *cl_val_bool(cl_eval_ctx_t *ctx, int b) {
    cl_value_t *v = cl_val_new(ctx, CL_VAL_BOOL);
    v->as.bool_value = b;
    return v;
}

cl_value_t *cl_val_null(cl_eval_ctx_t *ctx) {
    return cl_val_new(ctx, CL_VAL_NULL);
}

cl_value_t *cl_val_new_list(cl_eval_ctx_t *ctx) {
    cl_value_t *v = cl_val_new(ctx, CL_VAL_LIST);
    v->as.list.items = NULL;
    v->as.list.count = 0;
    v->as.list.capacity = 0;
    return v;
}

void cl_val_list_add(cl_eval_ctx_t *ctx, cl_value_t *list, cl_value_t *item) {
    cl_array_grow_raw(&ctx->result->arena, (void **)&list->as.list.items, &list->as.list.count,
                       &list->as.list.capacity, sizeof(cl_value_t *));
    list->as.list.items[list->as.list.count++] = item;
}

cl_value_t *cl_val_new_object(cl_eval_ctx_t *ctx) {
    cl_value_t *v = cl_val_new(ctx, CL_VAL_OBJECT);
    v->as.object.items = NULL;
    v->as.object.count = 0;
    v->as.object.capacity = 0;
    return v;
}

void cl_val_object_add(cl_eval_ctx_t *ctx, cl_value_t *obj, const char *key, cl_value_t *value) {
    cl_array_grow_raw(&ctx->result->arena, (void **)&obj->as.object.items, &obj->as.object.count,
                       &obj->as.object.capacity, sizeof(cl_value_object_item_t));
    cl_value_object_item_t *item = &obj->as.object.items[obj->as.object.count++];
    item->key = cl_arena_strdup_raw(&ctx->result->arena, key);
    item->value = value;
}

const char *cl_val_require_string(cl_eval_ctx_t *ctx, const cl_value_t *v, int line, int col) {
    switch (v->kind) {
        case CL_VAL_STRING:
            return v->as.string_value;
        case CL_VAL_NUMBER: {
            char tmp[64];
            if (cl_format_number(tmp, sizeof(tmp), v->as.number_value) != 0) {
                cl_eval_fail(ctx, line, col, "cannot convert NaN or infinity to string");
                return NULL;
            }
            return cl_arena_strdup_raw(&ctx->result->arena, tmp);
        }
        case CL_VAL_BOOL:
            return v->as.bool_value ? "true" : "false";
        default:
            cl_eval_fail(ctx, line, col, "cannot convert this value to string");
            return NULL;
    }
}

int cl_value_equal(const cl_value_t *a, const cl_value_t *b) {
    if (a->kind != b->kind) {
        return 0;
    }
    switch (a->kind) {
        case CL_VAL_STRING: return strcmp(a->as.string_value, b->as.string_value) == 0;
        case CL_VAL_NUMBER: return a->as.number_value == b->as.number_value;
        case CL_VAL_BOOL: return a->as.bool_value == b->as.bool_value;
        case CL_VAL_NULL: return 1;
        case CL_VAL_LIST:
            if (a->as.list.count != b->as.list.count) {
                return 0;
            }
            for (size_t i = 0; i < a->as.list.count; i++) {
                if (!cl_value_equal(a->as.list.items[i], b->as.list.items[i])) {
                    return 0;
                }
            }
            return 1;
        case CL_VAL_OBJECT: {
            if (a->as.object.count != b->as.object.count) {
                return 0;
            }
            for (size_t i = 0; i < a->as.object.count; i++) {
                const cl_value_t *bv = cl_value_object_get(b, a->as.object.items[i].key);
                if (!bv || !cl_value_equal(a->as.object.items[i].value, bv)) {
                    return 0;
                }
            }
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Scope (for-loop variable bindings)                                   */
/* ------------------------------------------------------------------ */

static cl_value_t *cl_scope_lookup(const cl_eval_scope_t *scope, const char *name) {
    for (const cl_eval_scope_t *s = scope; s; s = s->parent) {
        if (s->key_name && strcmp(s->key_name, name) == 0) {
            return s->key_value;
        }
        if (s->val_name && strcmp(s->val_name, name) == 0) {
            return s->val_value;
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* External bindings                                                    */
/* ------------------------------------------------------------------ */

/* Deep-copies a host-built value (a binding, or what a host function hands
 * to cl_call_copy()) into the result arena, so the evaluated tree never
 * points into the cl_bindings_t (which the host may free as soon as
 * evaluation returns) or into host memory. */
cl_value_t *cl_val_copy(cl_eval_ctx_t *ctx, const cl_value_t *v) {
    switch (v->kind) {
        case CL_VAL_STRING: return cl_val_string(ctx, v->as.string_value);
        case CL_VAL_NUMBER: return cl_val_number(ctx, v->as.number_value);
        case CL_VAL_BOOL: return cl_val_bool(ctx, v->as.bool_value);
        case CL_VAL_NULL: return cl_val_null(ctx);
        case CL_VAL_LIST: {
            cl_value_t *list = cl_val_new_list(ctx);
            for (size_t i = 0; i < v->as.list.count; i++) {
                cl_val_list_add(ctx, list, cl_val_copy(ctx, v->as.list.items[i]));
            }
            return list;
        }
        case CL_VAL_OBJECT: {
            cl_value_t *obj = cl_val_new_object(ctx);
            for (size_t i = 0; i < v->as.object.count; i++) {
                cl_val_object_add(ctx, obj, v->as.object.items[i].key, cl_val_copy(ctx, v->as.object.items[i].value));
            }
            return obj;
        }
    }
    return NULL;
}

/* The value the host bound to `name`, copied into the result on first use,
 * or NULL when there is no such binding. */
static cl_value_t *cl_eval_binding(cl_eval_ctx_t *ctx, const char *name) {
    if (!ctx->bindings) {
        return NULL;
    }
    for (size_t i = 0; i < ctx->bindings->count; i++) {
        if (strcmp(ctx->bindings->items[i].name, name) == 0) {
            if (!ctx->bound_copies[i]) {
                ctx->bound_copies[i] = cl_val_copy(ctx, ctx->bindings->items[i].value);
            }
            return ctx->bound_copies[i];
        }
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Core expression evaluator                                            */
/* ------------------------------------------------------------------ */

static cl_value_t *cl_eval_expr(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, const cl_expr_t *expr);

static cl_value_t *cl_eval_body_as_object(cl_eval_ctx_t *ctx, const cl_body_t *body) {
    cl_value_t *obj = cl_val_new_object(ctx);
    for (size_t i = 0; i < body->count; i++) {
        if (body->items[i].kind != CL_ITEM_ATTRIBUTE) {
            continue; /* nested blocks are not surfaced as object keys here */
        }
        cl_attribute_t *attr = body->items[i].as.attribute;
        cl_value_t *v = cl_eval_expr(ctx, NULL, attr->value);
        if (ctx->failed) {
            return NULL;
        }
        cl_val_object_add(ctx, obj, attr->name, v);
    }
    return obj;
}

static cl_value_t *cl_eval_index_list(cl_eval_ctx_t *ctx, cl_value_t *current, double index, int line, int col) {
    if (current->kind != CL_VAL_LIST) {
        cl_eval_fail(ctx, line, col, "cannot index: value is not a list");
        return NULL;
    }
    if (index != floor(index)) {
        cl_eval_fail(ctx, line, col, "index %g must be an integer", index);
        return NULL;
    }
    if (index < 0 || index >= (double)current->as.list.count) {
        cl_eval_fail(ctx, line, col, "index %g out of range (list has %zu items)", index,
                     current->as.list.count);
        return NULL;
    }
    return current->as.list.items[(size_t)index];
}

static cl_value_t *cl_eval_index_object(cl_eval_ctx_t *ctx, cl_value_t *current, const char *key, int line,
                                        int col) {
    if (current->kind != CL_VAL_OBJECT) {
        cl_eval_fail(ctx, line, col, "cannot access '[\"%s\"]': value is not an object", key);
        return NULL;
    }
    cl_value_t *v = cl_value_object_get(current, key);
    if (!v) {
        cl_eval_fail(ctx, line, col, "key '%s' not found", key);
        return NULL;
    }
    return v;
}

static cl_value_t *cl_eval_apply_step(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, cl_value_t *current,
                                       const cl_traversal_step_t *step, int line, int col) {
    switch (step->kind) {
        case CL_STEP_ATTR: {
            if (current->kind != CL_VAL_OBJECT) {
                cl_eval_fail(ctx, line, col, "cannot access '.%s': value is not an object", step->name);
                return NULL;
            }
            cl_value_t *v = cl_value_object_get(current, step->name);
            if (!v) {
                cl_eval_fail(ctx, line, col, "key '%s' not found", step->name);
                return NULL;
            }
            return v;
        }
        case CL_STEP_INDEX_NUMBER:
            return cl_eval_index_list(ctx, current, step->index, line, col);
        case CL_STEP_INDEX_STRING:
            return cl_eval_index_object(ctx, current, step->name, line, col);
        case CL_STEP_INDEX_EXPR: {
            /* Evaluated in the caller's scope, so a for-variable works as an
             * index ("zones[i]"); the kind of the resulting value, not the
             * syntax, picks list indexing vs. object key lookup. */
            cl_value_t *key = cl_eval_expr(ctx, scope, step->expr);
            if (ctx->failed) {
                return NULL;
            }
            if (key->kind == CL_VAL_NUMBER) {
                return cl_eval_index_list(ctx, current, key->as.number_value, step->expr->line, step->expr->col);
            }
            if (key->kind == CL_VAL_STRING) {
                return cl_eval_index_object(ctx, current, key->as.string_value, step->expr->line, step->expr->col);
            }
            cl_eval_fail(ctx, step->expr->line, step->expr->col, "index must be a number or a string");
            return NULL;
        }
        case CL_STEP_SPLAT_ATTR:
        case CL_STEP_SPLAT_FULL:
            /* handled by the caller, which maps the remaining steps */
            return NULL;
    }
    return NULL;
}

/* Shared tail end of both CL_EXPR_TRAVERSAL and CL_EXPR_POSTFIX: applies
 * steps[start_idx..step_count) to an already-resolved base value. */
static cl_value_t *cl_eval_apply_steps(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, cl_value_t *current,
                                        const cl_traversal_step_t *steps, size_t step_count, size_t start_idx,
                                        int line, int col) {
    for (size_t idx = start_idx; idx < step_count; idx++) {
        const cl_traversal_step_t *step = &steps[idx];
        if (step->kind == CL_STEP_SPLAT_ATTR || step->kind == CL_STEP_SPLAT_FULL) {
            cl_value_t *list_val = current;
            if (list_val->kind != CL_VAL_LIST) {
                cl_value_t *wrapped = cl_val_new_list(ctx);
                cl_val_list_add(ctx, wrapped, list_val);
                list_val = wrapped;
            }
            cl_value_t *result = cl_val_new_list(ctx);
            for (size_t e = 0; e < list_val->as.list.count; e++) {
                cl_value_t *elem = list_val->as.list.items[e];
                for (size_t j = idx + 1; j < step_count; j++) {
                    elem = cl_eval_apply_step(ctx, scope, elem, &steps[j], line, col);
                    if (ctx->failed) {
                        return NULL;
                    }
                }
                cl_val_list_add(ctx, result, elem);
            }
            return result;
        }
        current = cl_eval_apply_step(ctx, scope, current, step, line, col);
        if (ctx->failed) {
            return NULL;
        }
    }
    return current;
}

/* True when `key` (a cl_attribute_t* or cl_block_t* identity) is already
 * being resolved somewhere up the current call chain - i.e. resolving it
 * again from here would recurse forever. */
static int cl_eval_is_resolving(const cl_eval_resolving_t *stack, const void *key) {
    for (const cl_eval_resolving_t *f = stack; f; f = f->parent) {
        if (f->key == key) {
            return 1;
        }
    }
    return 0;
}

/* The value of label `index` of `block`: the stored text for a constant
 * label, or its template evaluated now for a computed one. Labels are
 * evaluated at the top level (no "for" scope). NULL, with ctx->failed set,
 * on an evaluation error - including a label whose value depends on
 * itself, e.g. `server "${server.x.port}" {}`. */
static const char *cl_eval_label(cl_eval_ctx_t *ctx, const cl_block_t *block, size_t index) {
    if (!block->label_exprs || !block->label_exprs[index]) {
        return block->labels[index];
    }
    const cl_expr_t *label = block->label_exprs[index];
    if (cl_eval_is_resolving(ctx->resolving, label)) {
        cl_eval_fail(ctx, label->line, label->col, "circular reference in label of block '%s'", block->type);
        return NULL;
    }
    cl_eval_resolving_t frame = {ctx->resolving, label};
    ctx->resolving = &frame;
    cl_value_t *v = cl_eval_expr(ctx, NULL, label);
    ctx->resolving = frame.parent;
    if (ctx->failed) {
        return NULL;
    }
    return cl_val_require_string(ctx, v, label->line, label->col);
}

/* Block-root fallback of traversal resolution: finds a top-level block
 * typed `root_name` whose labels match a PREFIX of `steps`, consuming as
 * many leading CL_STEP_ATTR steps as that block declares labels. Blocks
 * with different label counts can share the same type name, so this tries
 * every distinct label count found among same-typed blocks, longest first,
 * and returns the first one whose label values actually match - i.e. a
 * 2-label block like `resource "type" "name" {}` wins over a coincidental
 * 1-label block of the same type when both exist, instead of the 1-label
 * fallback silently winning by being tried first. Sets *out_label_count to
 * how many leading steps were consumed. */
static cl_block_t *cl_eval_find_block_by_labels(cl_eval_ctx_t *ctx, const char *root_name,
                                                 const cl_traversal_step_t *steps, size_t step_count,
                                                 size_t *out_label_count) {
    cl_block_t **candidates = NULL;
    size_t candidate_count = cl_body_find_blocks(ctx->root_scope, root_name, &candidates);

    size_t max_labels = 0;
    for (size_t i = 0; i < candidate_count; i++) {
        if (candidates[i]->label_count > max_labels) {
            max_labels = candidates[i]->label_count;
        }
    }
    if (max_labels > step_count) {
        max_labels = step_count;
    }

    cl_block_t *matched = NULL;
    for (size_t n = max_labels; !matched && n >= 1; n--) {
        int steps_are_labels = 1;
        for (size_t s = 0; s < n; s++) {
            if (steps[s].kind != CL_STEP_ATTR) {
                steps_are_labels = 0;
                break;
            }
        }
        if (!steps_are_labels) {
            continue;
        }
        for (size_t i = 0; i < candidate_count; i++) {
            if (candidates[i]->label_count != n) {
                continue;
            }
            int all_match = 1;
            for (size_t s = 0; s < n; s++) {
                const char *label = cl_eval_label(ctx, candidates[i], s);
                if (!label) {
                    free(candidates); /* evaluation error, already reported */
                    return NULL;
                }
                if (strcmp(label, steps[s].name) != 0) {
                    all_match = 0;
                    break;
                }
            }
            if (all_match) {
                matched = candidates[i];
                *out_label_count = n;
                break;
            }
        }
    }

    free(candidates);
    return matched;
}

static cl_value_t *cl_eval_traversal(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, const cl_expr_t *expr) {
    const char *root_name = expr->as.traversal.root;
    size_t step_count = expr->as.traversal.count;
    const cl_traversal_step_t *steps = expr->as.traversal.steps;
    size_t idx = 0;
    cl_value_t *current;

    cl_value_t *local = cl_scope_lookup(scope, root_name);
    cl_value_t *bound = local ? NULL : cl_eval_binding(ctx, root_name);
    if (local) {
        current = local;
    } else if (bound) {
        current = bound;
    } else {
        cl_attribute_t *attr = cl_body_get_attribute(ctx->root_scope, root_name);
        if (attr) {
            if (cl_eval_is_resolving(ctx->resolving, attr)) {
                cl_eval_fail(ctx, expr->line, expr->col, "circular reference involving '%s'", root_name);
                return NULL;
            }
            cl_eval_resolving_t frame = {ctx->resolving, attr};
            ctx->resolving = &frame;
            current = cl_eval_expr(ctx, NULL, attr->value);
            ctx->resolving = frame.parent;
            if (ctx->failed) {
                return NULL;
            }
        } else {
            if (step_count == 0 || steps[0].kind != CL_STEP_ATTR) {
                cl_eval_fail(ctx, expr->line, expr->col, "reference '%s' not found", root_name);
                return NULL;
            }
            size_t consumed = 0;
            cl_block_t *block = cl_eval_find_block_by_labels(ctx, root_name, steps, step_count, &consumed);
            if (ctx->failed) {
                return NULL;
            }
            if (!block) {
                cl_eval_fail(ctx, expr->line, expr->col, "reference '%s.%s' not found", root_name,
                             steps[0].name);
                return NULL;
            }
            if (cl_eval_is_resolving(ctx->resolving, block)) {
                cl_eval_fail(ctx, expr->line, expr->col, "circular reference involving '%s.%s'", root_name,
                             steps[0].name);
                return NULL;
            }
            cl_eval_resolving_t frame = {ctx->resolving, block};
            ctx->resolving = &frame;
            current = cl_eval_body_as_object(ctx, block->body);
            ctx->resolving = frame.parent;
            if (ctx->failed) {
                return NULL;
            }
            idx = consumed;
        }
    }

    return cl_eval_apply_steps(ctx, scope, current, steps, step_count, idx, expr->line, expr->col);
}

static cl_value_t *cl_eval_postfix(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, const cl_expr_t *expr) {
    cl_value_t *base = cl_eval_expr(ctx, scope, expr->as.postfix.base);
    if (ctx->failed) {
        return NULL;
    }
    return cl_eval_apply_steps(ctx, scope, base, expr->as.postfix.steps, expr->as.postfix.count, 0, expr->line,
                               expr->col);
}

static int cl_eval_render_template_into(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, const cl_template_t *tpl,
                                         char **buf, size_t *len, size_t *capacity);

static void cl_render_buf_append(char **buf, size_t *len, size_t *capacity, const char *s, size_t n) {
    if (*len + n + 1 > *capacity) {
        size_t new_capacity = *capacity ? *capacity * 2 : 64;
        while (new_capacity < *len + n + 1) {
            new_capacity *= 2;
        }
        char *new_buf = realloc(*buf, new_capacity);
        if (!new_buf) {
            abort();
        }
        *buf = new_buf;
        *capacity = new_capacity;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
}

static int cl_eval_render_template_into(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, const cl_template_t *tpl,
                                         char **buf, size_t *len, size_t *capacity) {
    for (size_t i = 0; i < tpl->count; i++) {
        const cl_template_part_t *part = &tpl->parts[i];
        switch (part->kind) {
            case CL_TPL_LITERAL:
                cl_render_buf_append(buf, len, capacity, part->text, strlen(part->text));
                break;
            case CL_TPL_INTERP: {
                cl_value_t *v = cl_eval_expr(ctx, scope, part->expr);
                if (ctx->failed) {
                    return -1;
                }
                const char *s = cl_val_require_string(ctx, v, part->expr->line, part->expr->col);
                if (!s) {
                    return -1;
                }
                cl_render_buf_append(buf, len, capacity, s, strlen(s));
                break;
            }
            case CL_TPL_IF: {
                cl_value_t *c = cl_eval_expr(ctx, scope, part->if_cond);
                if (ctx->failed) {
                    return -1;
                }
                if (c->kind != CL_VAL_BOOL) {
                    cl_eval_fail(ctx, part->if_cond->line, part->if_cond->col, "%%{if} condition must be a bool");
                    return -1;
                }
                if (c->as.bool_value) {
                    if (cl_eval_render_template_into(ctx, scope, part->if_then, buf, len, capacity) != 0) {
                        return -1;
                    }
                } else if (part->if_else) {
                    if (cl_eval_render_template_into(ctx, scope, part->if_else, buf, len, capacity) != 0) {
                        return -1;
                    }
                }
                break;
            }
            case CL_TPL_FOR: {
                cl_value_t *coll = cl_eval_expr(ctx, scope, part->for_collection);
                if (ctx->failed) {
                    return -1;
                }
                if (coll->kind != CL_VAL_LIST && coll->kind != CL_VAL_OBJECT) {
                    cl_eval_fail(ctx, part->for_collection->line, part->for_collection->col,
                                 "%%{for} expects a list or an object");
                    return -1;
                }
                size_t n = (coll->kind == CL_VAL_LIST) ? coll->as.list.count : coll->as.object.count;
                for (size_t e = 0; e < n; e++) {
                    cl_eval_scope_t frame = {0};
                    frame.parent = scope;
                    if (coll->kind == CL_VAL_LIST) {
                        if (part->for_key_var) {
                            frame.key_name = part->for_key_var;
                            frame.key_value = cl_val_number(ctx, (double)e);
                        }
                        frame.val_name = part->for_val_var;
                        frame.val_value = coll->as.list.items[e];
                    } else {
                        if (part->for_key_var) {
                            frame.key_name = part->for_key_var;
                            frame.key_value = cl_val_string(ctx, coll->as.object.items[e].key);
                        }
                        frame.val_name = part->for_val_var;
                        frame.val_value = coll->as.object.items[e].value;
                    }
                    if (cl_eval_render_template_into(ctx, &frame, part->for_body, buf, len, capacity) != 0) {
                        return -1;
                    }
                }
                break;
            }
        }
    }
    return 0;
}

static cl_value_t *cl_eval_template(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, const cl_expr_t *expr) {
    char *buf = NULL;
    size_t len = 0;
    size_t capacity = 0;
    if (cl_eval_render_template_into(ctx, scope, expr->as.tpl, &buf, &len, &capacity) != 0) {
        free(buf);
        return NULL;
    }
    cl_value_t *result = cl_val_string(ctx, buf ? buf : "");
    free(buf);
    return result;
}

static cl_value_t *cl_eval_call(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, const cl_expr_t *expr) {
    /* A host function shadows a built-in of the same name. */
    const char *name = expr->as.call.name;
    const cl_host_function_t *host = cl_bindings_lookup_function(ctx->bindings, name);
    const cl_builtin_t *builtin = host ? NULL : cl_builtin_lookup(name);
    if (!host && !builtin) {
        cl_eval_fail(ctx, expr->line, expr->col, "unknown function '%s'", expr->as.call.name);
        return NULL;
    }

    cl_value_t **args = NULL;
    size_t argc = 0;
    size_t arg_capacity = 0;

    for (size_t i = 0; i < expr->as.call.count; i++) {
        cl_value_t *v = cl_eval_expr(ctx, scope, expr->as.call.args[i]);
        if (ctx->failed) {
            free(args);
            return NULL;
        }
        if (expr->as.call.expand_final && i + 1 == expr->as.call.count) {
            if (v->kind != CL_VAL_LIST) {
                free(args);
                cl_eval_fail(ctx, expr->line, expr->col, "'...' expects a list as the last argument");
                return NULL;
            }
            for (size_t j = 0; j < v->as.list.count; j++) {
                if (argc == arg_capacity) {
                    arg_capacity = arg_capacity ? arg_capacity * 2 : 4;
                    cl_value_t **grown = realloc(args, arg_capacity * sizeof(cl_value_t *));
                    if (!grown) {
                        abort();
                    }
                    args = grown;
                }
                args[argc++] = v->as.list.items[j];
            }
        } else {
            if (argc == arg_capacity) {
                arg_capacity = arg_capacity ? arg_capacity * 2 : 4;
                cl_value_t **grown = realloc(args, arg_capacity * sizeof(cl_value_t *));
                if (!grown) {
                    abort();
                }
                args = grown;
            }
            args[argc++] = v;
        }
    }

    size_t min_args = host ? host->min_args : builtin->min_args;
    size_t max_args = host ? host->max_args : builtin->max_args;
    if (argc < min_args || argc > max_args) {
        cl_function_arity_fail(ctx, name, min_args, max_args, argc, expr->line, expr->col);
        free(args);
        return NULL;
    }

    cl_value_t *result = host ? cl_call_invoke(ctx, host, args, argc, expr->line, expr->col)
                              : builtin->fn(ctx, args, argc, expr->line, expr->col);
    free(args);
    return result;
}

static cl_value_t *cl_eval_unary(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, const cl_expr_t *expr) {
    cl_value_t *operand = cl_eval_expr(ctx, scope, expr->as.unary.operand);
    if (ctx->failed) {
        return NULL;
    }
    if (expr->as.unary.op == CL_OP_NEG) {
        if (operand->kind != CL_VAL_NUMBER) {
            cl_eval_fail(ctx, expr->line, expr->col, "unary operator '-' expects a number");
            return NULL;
        }
        return cl_val_number(ctx, -operand->as.number_value);
    }
    if (operand->kind != CL_VAL_BOOL) {
        cl_eval_fail(ctx, expr->line, expr->col, "unary operator '!' expects a bool");
        return NULL;
    }
    return cl_val_bool(ctx, !operand->as.bool_value);
}

static const char *cl_eval_binary_op_name(cl_binary_op_t op) {
    switch (op) {
        case CL_OP_ADD: return "+";
        case CL_OP_SUB: return "-";
        case CL_OP_MUL: return "*";
        case CL_OP_DIV: return "/";
        case CL_OP_MOD: return "%";
        case CL_OP_EQ: return "==";
        case CL_OP_NEQ: return "!=";
        case CL_OP_LT: return "<";
        case CL_OP_LE: return "<=";
        case CL_OP_GT: return ">";
        case CL_OP_GE: return ">=";
        case CL_OP_AND: return "&&";
        case CL_OP_OR: return "||";
    }
    return "?";
}

static cl_value_t *cl_eval_binary(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, const cl_expr_t *expr) {
    cl_binary_op_t op = expr->as.binary.op;

    if (op == CL_OP_AND || op == CL_OP_OR) {
        cl_value_t *l = cl_eval_expr(ctx, scope, expr->as.binary.left);
        if (ctx->failed) {
            return NULL;
        }
        if (l->kind != CL_VAL_BOOL) {
            cl_eval_fail(ctx, expr->line, expr->col, "logical operator expects a bool");
            return NULL;
        }
        if (op == CL_OP_AND && !l->as.bool_value) {
            return cl_val_bool(ctx, 0);
        }
        if (op == CL_OP_OR && l->as.bool_value) {
            return cl_val_bool(ctx, 1);
        }
        cl_value_t *r = cl_eval_expr(ctx, scope, expr->as.binary.right);
        if (ctx->failed) {
            return NULL;
        }
        if (r->kind != CL_VAL_BOOL) {
            cl_eval_fail(ctx, expr->line, expr->col, "logical operator expects a bool");
            return NULL;
        }
        return cl_val_bool(ctx, r->as.bool_value);
    }

    cl_value_t *l = cl_eval_expr(ctx, scope, expr->as.binary.left);
    if (ctx->failed) {
        return NULL;
    }
    cl_value_t *r = cl_eval_expr(ctx, scope, expr->as.binary.right);
    if (ctx->failed) {
        return NULL;
    }

    if (op == CL_OP_EQ) {
        return cl_val_bool(ctx, cl_value_equal(l, r));
    }
    if (op == CL_OP_NEQ) {
        return cl_val_bool(ctx, !cl_value_equal(l, r));
    }

    if (l->kind != CL_VAL_NUMBER || r->kind != CL_VAL_NUMBER) {
        cl_eval_fail(ctx, expr->line, expr->col, "operator '%s' expects numbers", cl_eval_binary_op_name(op));
        return NULL;
    }
    double a = l->as.number_value;
    double b = r->as.number_value;
    switch (op) {
        case CL_OP_ADD: return cl_val_number(ctx, a + b);
        case CL_OP_SUB: return cl_val_number(ctx, a - b);
        case CL_OP_MUL: return cl_val_number(ctx, a * b);
        case CL_OP_DIV:
            if (b == 0) {
                cl_eval_fail(ctx, expr->line, expr->col, "division by zero");
                return NULL;
            }
            return cl_val_number(ctx, a / b);
        case CL_OP_MOD:
            if (b == 0) {
                cl_eval_fail(ctx, expr->line, expr->col, "division by zero");
                return NULL;
            }
            return cl_val_number(ctx, fmod(a, b));
        case CL_OP_LT: return cl_val_bool(ctx, a < b);
        case CL_OP_LE: return cl_val_bool(ctx, a <= b);
        case CL_OP_GT: return cl_val_bool(ctx, a > b);
        case CL_OP_GE: return cl_val_bool(ctx, a >= b);
        default: return NULL; /* CL_OP_AND/OR/EQ/NEQ handled above */
    }
}

static cl_value_t *cl_eval_for(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, const cl_expr_t *expr) {
    cl_value_t *coll = cl_eval_expr(ctx, scope, expr->as.for_expr.collection);
    if (ctx->failed) {
        return NULL;
    }
    if (coll->kind != CL_VAL_LIST && coll->kind != CL_VAL_OBJECT) {
        cl_eval_fail(ctx, expr->line, expr->col, "for-expression expects a list or an object");
        return NULL;
    }

    cl_value_t *result = expr->as.for_expr.is_object ? cl_val_new_object(ctx) : cl_val_new_list(ctx);
    size_t n = (coll->kind == CL_VAL_LIST) ? coll->as.list.count : coll->as.object.count;

    for (size_t i = 0; i < n; i++) {
        cl_eval_scope_t frame = {0};
        frame.parent = scope;
        if (coll->kind == CL_VAL_LIST) {
            if (expr->as.for_expr.key_var) {
                frame.key_name = expr->as.for_expr.key_var;
                frame.key_value = cl_val_number(ctx, (double)i);
            }
            frame.val_name = expr->as.for_expr.val_var;
            frame.val_value = coll->as.list.items[i];
        } else {
            if (expr->as.for_expr.key_var) {
                frame.key_name = expr->as.for_expr.key_var;
                frame.key_value = cl_val_string(ctx, coll->as.object.items[i].key);
            }
            frame.val_name = expr->as.for_expr.val_var;
            frame.val_value = coll->as.object.items[i].value;
        }

        if (expr->as.for_expr.cond) {
            cl_value_t *cond_val = cl_eval_expr(ctx, &frame, expr->as.for_expr.cond);
            if (ctx->failed) {
                return NULL;
            }
            if (cond_val->kind != CL_VAL_BOOL) {
                cl_eval_fail(ctx, expr->line, expr->col, "for-expression 'if' filter must be a bool");
                return NULL;
            }
            if (!cond_val->as.bool_value) {
                continue;
            }
        }

        if (expr->as.for_expr.is_object) {
            cl_value_t *key_val = cl_eval_expr(ctx, &frame, expr->as.for_expr.key_expr);
            if (ctx->failed) {
                return NULL;
            }
            if (key_val->kind != CL_VAL_STRING) {
                cl_eval_fail(ctx, expr->line, expr->col, "object for-expression key must be a string");
                return NULL;
            }
            cl_value_t *value_val = cl_eval_expr(ctx, &frame, expr->as.for_expr.value_expr);
            if (ctx->failed) {
                return NULL;
            }
            cl_value_t *existing = cl_value_object_get(result, key_val->as.string_value);
            if (existing) {
                if (!expr->as.for_expr.grouping) {
                    cl_eval_fail(ctx, expr->line, expr->col, "duplicate key '%s' in object for-expression",
                                 key_val->as.string_value);
                    return NULL;
                }
                cl_val_list_add(ctx, existing, value_val);
            } else if (expr->as.for_expr.grouping) {
                cl_value_t *group = cl_val_new_list(ctx);
                cl_val_list_add(ctx, group, value_val);
                cl_val_object_add(ctx, result, key_val->as.string_value, group);
            } else {
                cl_val_object_add(ctx, result, key_val->as.string_value, value_val);
            }
        } else {
            cl_value_t *value_val = cl_eval_expr(ctx, &frame, expr->as.for_expr.value_expr);
            if (ctx->failed) {
                return NULL;
            }
            cl_val_list_add(ctx, result, value_val);
        }
    }
    return result;
}

static cl_value_t *cl_eval_expr(cl_eval_ctx_t *ctx, const cl_eval_scope_t *scope, const cl_expr_t *expr) {
    if (ctx->failed) {
        return NULL;
    }
    switch (expr->kind) {
        case CL_EXPR_STRING: return cl_val_string(ctx, expr->as.string_value);
        case CL_EXPR_NUMBER: return cl_val_number(ctx, expr->as.number_value);
        case CL_EXPR_BOOL: return cl_val_bool(ctx, expr->as.bool_value);
        case CL_EXPR_NULL: return cl_val_null(ctx);
        case CL_EXPR_OBJECT: {
            cl_value_t *obj = cl_val_new_object(ctx);
            for (size_t i = 0; i < expr->as.object.count; i++) {
                const cl_object_item_t *item = &expr->as.object.items[i];
                const char *key = item->key;
                if (item->key_expr) {
                    cl_value_t *k = cl_eval_expr(ctx, scope, item->key_expr);
                    if (ctx->failed) {
                        return NULL;
                    }
                    if (k->kind != CL_VAL_STRING && k->kind != CL_VAL_NUMBER && k->kind != CL_VAL_BOOL) {
                        cl_eval_fail(ctx, item->key_expr->line, item->key_expr->col,
                                     "object key must be a string, number or bool");
                        return NULL;
                    }
                    key = cl_val_require_string(ctx, k, item->key_expr->line, item->key_expr->col);
                    if (!key) {
                        return NULL;
                    }
                }
                if (cl_value_object_get(obj, key)) {
                    const cl_expr_t *at = item->key_expr ? item->key_expr : item->value;
                    cl_eval_fail(ctx, at->line, at->col, "duplicate key '%s' in object", key);
                    return NULL;
                }
                cl_value_t *v = cl_eval_expr(ctx, scope, item->value);
                if (ctx->failed) {
                    return NULL;
                }
                cl_val_object_add(ctx, obj, key, v);
            }
            return obj;
        }
        case CL_EXPR_TUPLE: {
            cl_value_t *list = cl_val_new_list(ctx);
            for (size_t i = 0; i < expr->as.tuple.count; i++) {
                cl_value_t *v = cl_eval_expr(ctx, scope, expr->as.tuple.items[i]);
                if (ctx->failed) {
                    return NULL;
                }
                cl_val_list_add(ctx, list, v);
            }
            return list;
        }
        case CL_EXPR_TRAVERSAL: return cl_eval_traversal(ctx, scope, expr);
        case CL_EXPR_POSTFIX: return cl_eval_postfix(ctx, scope, expr);
        case CL_EXPR_TEMPLATE: return cl_eval_template(ctx, scope, expr);
        case CL_EXPR_CALL: return cl_eval_call(ctx, scope, expr);
        case CL_EXPR_UNARY: return cl_eval_unary(ctx, scope, expr);
        case CL_EXPR_BINARY: return cl_eval_binary(ctx, scope, expr);
        case CL_EXPR_CONDITIONAL: {
            cl_value_t *c = cl_eval_expr(ctx, scope, expr->as.conditional.cond);
            if (ctx->failed) {
                return NULL;
            }
            if (c->kind != CL_VAL_BOOL) {
                cl_eval_fail(ctx, expr->line, expr->col, "'?:' condition must be a bool");
                return NULL;
            }
            return cl_eval_expr(ctx, scope, c->as.bool_value ? expr->as.conditional.then_expr
                                                               : expr->as.conditional.else_expr);
        }
        case CL_EXPR_FOR: return cl_eval_for(ctx, scope, expr);
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Evaluated-tree construction                                          */
/* ------------------------------------------------------------------ */

/* `top_level` is true only for the document root, the one body whose
 * attributes a binding of the same name overrides. */
static cl_evaluated_body_t *cl_eval_build_body(cl_eval_ctx_t *ctx, const cl_body_t *body, int top_level) {
    cl_evaluated_body_t *out = cl_arena_alloc_raw(&ctx->result->arena, sizeof(cl_evaluated_body_t));
    out->items = NULL;
    out->count = 0;
    out->capacity = 0;

    for (size_t i = 0; i < body->count; i++) {
        if (ctx->failed) {
            return NULL;
        }
        const cl_body_item_t *item = &body->items[i];
        cl_array_grow_raw(&ctx->result->arena, (void **)&out->items, &out->count, &out->capacity,
                           sizeof(cl_evaluated_item_t));
        cl_evaluated_item_t *out_item = &out->items[out->count++];

        if (item->kind == CL_ITEM_ATTRIBUTE) {
            cl_value_t *v = top_level ? cl_eval_binding(ctx, item->as.attribute->name) : NULL;
            if (!v) {
                v = cl_eval_expr(ctx, NULL, item->as.attribute->value);
            }
            if (ctx->failed) {
                return NULL;
            }
            cl_evaluated_attribute_t *attr = cl_arena_alloc_raw(&ctx->result->arena, sizeof(cl_evaluated_attribute_t));
            attr->name = cl_arena_strdup_raw(&ctx->result->arena, item->as.attribute->name);
            attr->value = v;
            out_item->kind = CL_ITEM_ATTRIBUTE;
            out_item->as.attribute = attr;
        } else {
            const cl_block_t *block = item->as.block;
            cl_evaluated_block_t *eb = cl_arena_alloc_raw(&ctx->result->arena, sizeof(cl_evaluated_block_t));
            eb->type = cl_arena_strdup_raw(&ctx->result->arena, block->type);
            eb->label_count = block->label_count;
            eb->labels = NULL;
            if (block->label_count > 0) {
                eb->labels = cl_arena_alloc_raw(&ctx->result->arena, block->label_count * sizeof(char *));
                for (size_t l = 0; l < block->label_count; l++) {
                    const char *label = cl_eval_label(ctx, block, l);
                    if (!label) {
                        return NULL;
                    }
                    eb->labels[l] = cl_arena_strdup_raw(&ctx->result->arena, label);
                }
            }
            eb->body = cl_eval_build_body(ctx, block->body, 0);
            if (ctx->failed) {
                return NULL;
            }
            out_item->kind = CL_ITEM_BLOCK;
            out_item->as.block = eb;
        }
    }
    return out;
}

cl_evaluated_t *cl_document_evaluate(cl_document_t *doc, cl_error_t *err) {
    return cl_document_evaluate_with(doc, NULL, err);
}

cl_evaluated_t *cl_document_evaluate_with(cl_document_t *doc, const cl_bindings_t *bindings, cl_error_t *err) {
    cl_evaluated_t *result = calloc(1, sizeof(cl_evaluated_t));
    if (!result) {
        abort();
    }

    cl_eval_ctx_t ctx = {0};
    ctx.doc = doc;
    ctx.root_scope = doc->root;
    ctx.result = result;
    ctx.err = err;
    ctx.failed = 0;
    if (bindings && (bindings->count > 0 || bindings->function_count > 0)) {
        ctx.bindings = bindings;
    }
    if (bindings && bindings->count > 0) {
        ctx.bound_copies = cl_arena_alloc_raw(&result->arena, bindings->count * sizeof(cl_value_t *));
    }

    result->root = cl_eval_build_body(&ctx, doc->root, 1);

    if (ctx.failed) {
        cl_evaluated_free(result);
        return NULL;
    }
    return result;
}

void cl_evaluated_free(cl_evaluated_t *result) {
    if (!result) {
        return;
    }
    cl_arena_free_all(&result->arena);
    free(result);
}

cl_evaluated_body_t *cl_evaluated_root(cl_evaluated_t *result) {
    return result->root;
}

/* ------------------------------------------------------------------ */
/* Evaluated-tree navigation (mirrors cl_navigate.c)                    */
/* ------------------------------------------------------------------ */

cl_evaluated_attribute_t *cl_evaluated_body_get_attribute(const cl_evaluated_body_t *body, const char *name) {
    if (!body) {
        return NULL;
    }
    for (size_t i = 0; i < body->count; i++) {
        const cl_evaluated_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_ATTRIBUTE && strcmp(item->as.attribute->name, name) == 0) {
            return item->as.attribute;
        }
    }
    return NULL;
}

size_t cl_evaluated_body_find_blocks(const cl_evaluated_body_t *body, const char *type,
                                      cl_evaluated_block_t ***out_blocks) {
    if (out_blocks) {
        *out_blocks = NULL;
    }
    if (!body) {
        return 0;
    }
    size_t match_count = 0;
    for (size_t i = 0; i < body->count; i++) {
        const cl_evaluated_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_BLOCK && strcmp(item->as.block->type, type) == 0) {
            match_count++;
        }
    }
    if (match_count == 0 || !out_blocks) {
        return match_count;
    }
    cl_evaluated_block_t **matches = malloc(match_count * sizeof(cl_evaluated_block_t *));
    if (!matches) {
        abort();
    }
    size_t next = 0;
    for (size_t i = 0; i < body->count; i++) {
        const cl_evaluated_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_BLOCK && strcmp(item->as.block->type, type) == 0) {
            matches[next++] = item->as.block;
        }
    }
    *out_blocks = matches;
    return match_count;
}

static int cl_evaluated_labels_match(const cl_evaluated_block_t *block, const char *const *labels,
                                      size_t label_count) {
    if (block->label_count != label_count) {
        return 0;
    }
    for (size_t i = 0; i < label_count; i++) {
        if (strcmp(block->labels[i], labels[i]) != 0) {
            return 0;
        }
    }
    return 1;
}

cl_evaluated_block_t *cl_evaluated_body_find_block(const cl_evaluated_body_t *body, const char *type,
                                                    const char *const *labels, size_t label_count) {
    if (!body) {
        return NULL;
    }
    for (size_t i = 0; i < body->count; i++) {
        const cl_evaluated_item_t *item = &body->items[i];
        if (item->kind == CL_ITEM_BLOCK && strcmp(item->as.block->type, type) == 0 &&
            cl_evaluated_labels_match(item->as.block, labels, label_count)) {
            return item->as.block;
        }
    }
    return NULL;
}

cl_value_kind_t cl_value_kind(const cl_value_t *value) {
    return value->kind;
}

const char *cl_value_as_string(const cl_value_t *value) {
    if (!value || value->kind != CL_VAL_STRING) {
        return NULL;
    }
    return value->as.string_value;
}

int cl_value_as_number(const cl_value_t *value, double *out) {
    if (!value || value->kind != CL_VAL_NUMBER) {
        return -1;
    }
    if (out) {
        *out = value->as.number_value;
    }
    return 0;
}

int cl_value_as_bool(const cl_value_t *value, int *out) {
    if (!value || value->kind != CL_VAL_BOOL) {
        return -1;
    }
    if (out) {
        *out = value->as.bool_value;
    }
    return 0;
}

size_t cl_value_list_count(const cl_value_t *value) {
    if (!value || value->kind != CL_VAL_LIST) {
        return 0;
    }
    return value->as.list.count;
}

cl_value_t *cl_value_list_at(const cl_value_t *value, size_t index) {
    if (!value || value->kind != CL_VAL_LIST || index >= value->as.list.count) {
        return NULL;
    }
    return value->as.list.items[index];
}

size_t cl_value_object_count(const cl_value_t *value) {
    if (!value || value->kind != CL_VAL_OBJECT) {
        return 0;
    }
    return value->as.object.count;
}

const char *cl_value_object_key_at(const cl_value_t *value, size_t index) {
    if (!value || value->kind != CL_VAL_OBJECT || index >= value->as.object.count) {
        return NULL;
    }
    return value->as.object.items[index].key;
}

cl_value_t *cl_value_object_value_at(const cl_value_t *value, size_t index) {
    if (!value || value->kind != CL_VAL_OBJECT || index >= value->as.object.count) {
        return NULL;
    }
    return value->as.object.items[index].value;
}

cl_value_t *cl_value_object_get(const cl_value_t *value, const char *key) {
    if (!value || value->kind != CL_VAL_OBJECT) {
        return NULL;
    }
    for (size_t i = 0; i < value->as.object.count; i++) {
        if (strcmp(value->as.object.items[i].key, key) == 0) {
            return value->as.object.items[i].value;
        }
    }
    return NULL;
}
