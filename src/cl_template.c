#include "cl_template.h"

#include "cl_ast.h"
#include "cl_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
    int line;
    int col;
} cl_tpl_scanner_t;

static int cl_tpl_peek(const cl_tpl_scanner_t *s, size_t offset) {
    size_t at = s->pos + offset;
    if (at >= s->len) {
        return '\0';
    }
    return (unsigned char)s->src[at];
}

static void cl_tpl_advance(cl_tpl_scanner_t *s) {
    if (s->src[s->pos] == '\n') {
        s->line++;
        s->col = 1;
    } else {
        s->col++;
    }
    s->pos++;
}

static int cl_tpl_is_ident_start(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int cl_tpl_is_ident_continue(int c) {
    return cl_tpl_is_ident_start(c) || (c >= '0' && c <= '9') || c == '-';
}

static void cl_tpl_skip_ws(cl_tpl_scanner_t *s) {
    while (cl_tpl_peek(s, 0) == ' ' || cl_tpl_peek(s, 0) == '\t' || cl_tpl_peek(s, 0) == '\n' ||
           cl_tpl_peek(s, 0) == '\r') {
        cl_tpl_advance(s);
    }
}

static int cl_tpl_is_ws_char(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static char *cl_tpl_read_ident(cl_tpl_scanner_t *s) {
    size_t start = s->pos;
    if (!cl_tpl_is_ident_start(cl_tpl_peek(s, 0))) {
        return NULL;
    }
    cl_tpl_advance(s);
    while (cl_tpl_is_ident_continue(cl_tpl_peek(s, 0))) {
        cl_tpl_advance(s);
    }
    size_t len = s->pos - start;
    char *text = malloc(len + 1);
    if (!text) {
        abort();
    }
    memcpy(text, s->src + start, len);
    text[len] = '\0';
    return text;
}

/* Small growable buffer for the literal text currently being accumulated. */
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} cl_strbuf_t;

/* Trim marker support ("${~ expr ~}", "%{~if cond~}", ...): a "~" glued to
 * the inner side of "${"/"%{" strips trailing whitespace from whatever
 * literal text was already accumulated just before it; a "~" glued to the
 * inner side of the matching "}" strips leading whitespace from whatever
 * literal text follows. Both are applied eagerly, at the source-text level,
 * right where each marker is found - see cl_compile_template() below. */
static void cl_tpl_rtrim_buf(cl_strbuf_t *buf) {
    while (buf->len > 0 && cl_tpl_is_ws_char(buf->data[buf->len - 1])) {
        buf->len--;
    }
}

/* Consumes an optional right-trim "~" then the mandatory closing '}' of a
 * directive that takes no expression (%{else}, %{endif}, %{endfor}),
 * trimming the whitespace that follows when "~" was present. Returns -1,
 * leaving the scanner at the offending character, if '}' never shows up. */
static int cl_tpl_close_bare_directive(cl_tpl_scanner_t *s) {
    cl_tpl_skip_ws(s);
    int trim_right = 0;
    if (cl_tpl_peek(s, 0) == '~') {
        cl_tpl_advance(s);
        trim_right = 1;
        cl_tpl_skip_ws(s);
    }
    if (cl_tpl_peek(s, 0) != '}') {
        return -1;
    }
    cl_tpl_advance(s);
    if (trim_right) {
        cl_tpl_skip_ws(s);
    }
    return trim_right;
}

static void cl_strbuf_push(cl_strbuf_t *buf, char c) {
    if (buf->len == buf->capacity) {
        size_t new_capacity = buf->capacity ? buf->capacity * 2 : 32;
        char *new_data = realloc(buf->data, new_capacity);
        if (!new_data) {
            abort();
        }
        buf->data = new_data;
        buf->capacity = new_capacity;
    }
    buf->data[buf->len++] = c;
}

/* Scans forward from just after an opening "${"/"%{" (or after "in " in a
 * "%{for ... in COLLECTION}" clause) until the matching top-level '}',
 * skipping over nested "{...}" and quoted-string content so those don't
 * confuse the search. Returns the text in between (malloc'd, caller
 * frees) and leaves the scanner positioned right after the consumed '}'. */
static char *cl_tpl_extract_span(cl_tpl_scanner_t *s, cl_error_t *err) {
    size_t start = s->pos;
    int depth = 0;
    for (;;) {
        int c = cl_tpl_peek(s, 0);
        if (c == '\0') {
            if (err) {
                snprintf(err->message, sizeof(err->message), "esperado '}' para fechar a expressao");
                err->line = s->line;
                err->col = s->col;
            }
            return NULL;
        }
        if (c == '"') {
            cl_tpl_advance(s);
            while (cl_tpl_peek(s, 0) != '\0' && cl_tpl_peek(s, 0) != '"') {
                if (cl_tpl_peek(s, 0) == '\\' && cl_tpl_peek(s, 1) != '\0') {
                    cl_tpl_advance(s);
                }
                cl_tpl_advance(s);
            }
            if (cl_tpl_peek(s, 0) == '"') {
                cl_tpl_advance(s);
            }
            continue;
        }
        if (c == '{') {
            depth++;
            cl_tpl_advance(s);
            continue;
        }
        if (c == '}') {
            if (depth == 0) {
                size_t end = s->pos;
                char *text = malloc(end - start + 1);
                if (!text) {
                    abort();
                }
                memcpy(text, s->src + start, end - start);
                text[end - start] = '\0';
                cl_tpl_advance(s); /* consume the closing '}' */
                return text;
            }
            depth--;
            cl_tpl_advance(s);
            continue;
        }
        cl_tpl_advance(s);
    }
}

/* For "${expr ~}" / "%{if cond ~}" / "%{for v in coll ~}": cl_tpl_extract_span()
 * already consumed the closing '}', so the right-trim marker (if any) shows
 * up as a trailing "~" in the extracted span text itself. Strips it (and
 * any whitespace around it, so the remaining text is a clean expression
 * with no dangling newline token) and reports whether one was found. */
static int cl_tpl_strip_trailing_tilde(char *span) {
    size_t end = strlen(span);
    while (end > 0 && cl_tpl_is_ws_char(span[end - 1])) {
        end--;
    }
    if (end == 0 || span[end - 1] != '~') {
        return 0;
    }
    end--;
    while (end > 0 && cl_tpl_is_ws_char(span[end - 1])) {
        end--;
    }
    span[end] = '\0';
    return 1;
}

static cl_expr_t *cl_tpl_parse_span(cl_document_t *doc, char *span, int line, int col, cl_error_t *err) {
    cl_expr_t *expr = cl_parser_parse_expr_string(doc, span, line, col, err);
    free(span);
    return expr;
}

typedef struct cl_tpl_frame {
    int is_for;
    /* CL_TPL_IF */
    cl_expr_t *if_cond;
    cl_template_t *if_then;
    cl_template_t *if_else;
    int in_else;
    /* CL_TPL_FOR */
    char *for_key_var;
    char *for_val_var;
    cl_expr_t *for_collection;
    cl_template_t *for_body;
} cl_tpl_frame_t;

typedef struct {
    cl_tpl_frame_t *items;
    size_t count;
    size_t capacity;
} cl_tpl_stack_t;

static void cl_tpl_stack_push(cl_tpl_stack_t *stack, cl_tpl_frame_t frame) {
    if (stack->count == stack->capacity) {
        size_t new_capacity = stack->capacity ? stack->capacity * 2 : 8;
        cl_tpl_frame_t *new_items = realloc(stack->items, new_capacity * sizeof(cl_tpl_frame_t));
        if (!new_items) {
            abort();
        }
        stack->items = new_items;
        stack->capacity = new_capacity;
    }
    stack->items[stack->count++] = frame;
}

static cl_template_t *cl_tpl_current_target(cl_template_t *root, cl_tpl_stack_t *stack) {
    if (stack->count == 0) {
        return root;
    }
    cl_tpl_frame_t *top = &stack->items[stack->count - 1];
    if (top->is_for) {
        return top->for_body;
    }
    return top->in_else ? top->if_else : top->if_then;
}

static void cl_tpl_flush_literal(cl_document_t *doc, cl_template_t *root, cl_tpl_stack_t *stack,
                                  cl_strbuf_t *literal) {
    if (literal->len == 0) {
        return;
    }
    cl_strbuf_push(literal, '\0');
    cl_template_add_literal(doc, cl_tpl_current_target(root, stack), literal->data);
    literal->len = 0;
}

static int cl_tpl_fail(cl_error_t *err, cl_error_t *out, int line, int col, const char *message) {
    if (out) {
        if (err && err->message[0] != '\0') {
            *out = *err;
        } else {
            snprintf(out->message, sizeof(out->message), "%s", message);
            out->line = line;
            out->col = col;
        }
    }
    return -1;
}

cl_expr_t *cl_compile_template(cl_document_t *doc, const char *raw, int base_line, int base_col, cl_error_t *err) {
    cl_tpl_scanner_t s;
    s.src = raw;
    s.len = strlen(raw);
    s.pos = 0;
    s.line = base_line;
    s.col = base_col;

    cl_template_t *root = cl_template_new(doc);
    cl_tpl_stack_t stack = {0};
    cl_strbuf_t literal = {0};

    while (s.pos < s.len) {
        if (cl_tpl_peek(&s, 0) == '$' && cl_tpl_peek(&s, 1) == '$' && cl_tpl_peek(&s, 2) == '{') {
            cl_strbuf_push(&literal, '$');
            cl_strbuf_push(&literal, '{');
            cl_tpl_advance(&s);
            cl_tpl_advance(&s);
            cl_tpl_advance(&s);
            continue;
        }
        if (cl_tpl_peek(&s, 0) == '%' && cl_tpl_peek(&s, 1) == '%' && cl_tpl_peek(&s, 2) == '{') {
            cl_strbuf_push(&literal, '%');
            cl_strbuf_push(&literal, '{');
            cl_tpl_advance(&s);
            cl_tpl_advance(&s);
            cl_tpl_advance(&s);
            continue;
        }
        if (cl_tpl_peek(&s, 0) == '$' && cl_tpl_peek(&s, 1) == '{') {
            int trim_left = cl_tpl_peek(&s, 2) == '~';
            if (trim_left) {
                cl_tpl_rtrim_buf(&literal);
            }
            cl_tpl_flush_literal(doc, root, &stack, &literal);
            cl_tpl_advance(&s);
            cl_tpl_advance(&s);
            if (trim_left) {
                cl_tpl_advance(&s);
            }
            int expr_line = s.line;
            int expr_col = s.col;
            cl_error_t sub_err = {0};
            char *span = cl_tpl_extract_span(&s, &sub_err);
            if (!span) {
                free(literal.data);
                free(stack.items);
                cl_tpl_fail(&sub_err, err, expr_line, expr_col, "expressao invalida");
                return NULL;
            }
            int trim_right = cl_tpl_strip_trailing_tilde(span);
            cl_expr_t *expr = cl_tpl_parse_span(doc, span, expr_line, expr_col, &sub_err);
            if (!expr) {
                free(literal.data);
                free(stack.items);
                cl_tpl_fail(&sub_err, err, expr_line, expr_col, "expressao invalida");
                return NULL;
            }
            cl_template_add_interp(doc, cl_tpl_current_target(root, &stack), expr);
            if (trim_right) {
                cl_tpl_skip_ws(&s);
            }
            continue;
        }
        if (cl_tpl_peek(&s, 0) == '%' && cl_tpl_peek(&s, 1) == '{') {
            int trim_left = cl_tpl_peek(&s, 2) == '~';
            if (trim_left) {
                cl_tpl_rtrim_buf(&literal);
            }
            cl_tpl_flush_literal(doc, root, &stack, &literal);
            int dir_line = s.line;
            int dir_col = s.col;
            cl_tpl_advance(&s);
            cl_tpl_advance(&s);
            if (trim_left) {
                cl_tpl_advance(&s);
            }
            cl_tpl_skip_ws(&s);
            char *keyword = cl_tpl_read_ident(&s);
            if (!keyword) {
                free(literal.data);
                free(stack.items);
                if (err) {
                    snprintf(err->message, sizeof(err->message), "diretiva de template invalida");
                    err->line = dir_line;
                    err->col = dir_col;
                }
                return NULL;
            }

            if (strcmp(keyword, "if") == 0) {
                free(keyword);
                cl_tpl_skip_ws(&s);
                int cond_line = s.line, cond_col = s.col;
                cl_error_t sub_err = {0};
                char *span = cl_tpl_extract_span(&s, &sub_err);
                if (!span) {
                    free(literal.data);
                    free(stack.items);
                    cl_tpl_fail(&sub_err, err, cond_line, cond_col, "condicao invalida em %{if}");
                    return NULL;
                }
                int trim_right = cl_tpl_strip_trailing_tilde(span);
                cl_expr_t *cond = cl_tpl_parse_span(doc, span, cond_line, cond_col, &sub_err);
                if (!cond) {
                    free(literal.data);
                    free(stack.items);
                    cl_tpl_fail(&sub_err, err, cond_line, cond_col, "condicao invalida em %{if}");
                    return NULL;
                }
                cl_tpl_frame_t frame = {0};
                frame.is_for = 0;
                frame.if_cond = cond;
                frame.if_then = cl_template_new(doc);
                frame.if_else = NULL;
                frame.in_else = 0;
                cl_tpl_stack_push(&stack, frame);
                if (trim_right) {
                    cl_tpl_skip_ws(&s);
                }
            } else if (strcmp(keyword, "else") == 0) {
                free(keyword);
                if (cl_tpl_close_bare_directive(&s) < 0) {
                    free(literal.data);
                    free(stack.items);
                    if (err) {
                        snprintf(err->message, sizeof(err->message), "%%{else} nao aceita expressao");
                        err->line = s.line;
                        err->col = s.col;
                    }
                    return NULL;
                }
                if (stack.count == 0 || stack.items[stack.count - 1].is_for ||
                    stack.items[stack.count - 1].in_else) {
                    free(literal.data);
                    free(stack.items);
                    if (err) {
                        snprintf(err->message, sizeof(err->message), "%%{else} inesperado");
                        err->line = dir_line;
                        err->col = dir_col;
                    }
                    return NULL;
                }
                cl_tpl_frame_t *top = &stack.items[stack.count - 1];
                top->if_else = cl_template_new(doc);
                top->in_else = 1;
            } else if (strcmp(keyword, "endif") == 0) {
                free(keyword);
                if (cl_tpl_close_bare_directive(&s) < 0) {
                    free(literal.data);
                    free(stack.items);
                    if (err) {
                        snprintf(err->message, sizeof(err->message), "%%{endif} mal formado");
                        err->line = s.line;
                        err->col = s.col;
                    }
                    return NULL;
                }
                if (stack.count == 0 || stack.items[stack.count - 1].is_for) {
                    free(literal.data);
                    free(stack.items);
                    if (err) {
                        snprintf(err->message, sizeof(err->message), "%%{endif} inesperado");
                        err->line = dir_line;
                        err->col = dir_col;
                    }
                    return NULL;
                }
                cl_tpl_frame_t frame = stack.items[--stack.count];
                cl_template_add_if(doc, cl_tpl_current_target(root, &stack), frame.if_cond, frame.if_then,
                                    frame.if_else);
            } else if (strcmp(keyword, "for") == 0) {
                free(keyword);
                cl_tpl_skip_ws(&s);
                char *var1 = cl_tpl_read_ident(&s);
                if (!var1) {
                    free(literal.data);
                    free(stack.items);
                    if (err) {
                        snprintf(err->message, sizeof(err->message), "esperado variavel em %%{for}");
                        err->line = s.line;
                        err->col = s.col;
                    }
                    return NULL;
                }
                char *key_var = NULL;
                char *val_var = var1;
                cl_tpl_skip_ws(&s);
                if (cl_tpl_peek(&s, 0) == ',') {
                    cl_tpl_advance(&s);
                    cl_tpl_skip_ws(&s);
                    char *var2 = cl_tpl_read_ident(&s);
                    if (!var2) {
                        free(var1);
                        free(literal.data);
                        free(stack.items);
                        if (err) {
                            snprintf(err->message, sizeof(err->message), "esperado segunda variavel em %%{for}");
                            err->line = s.line;
                            err->col = s.col;
                        }
                        return NULL;
                    }
                    key_var = var1;
                    val_var = var2;
                    cl_tpl_skip_ws(&s);
                }
                char *in_kw = cl_tpl_read_ident(&s);
                if (!in_kw || strcmp(in_kw, "in") != 0) {
                    free(in_kw);
                    free(key_var);
                    free(val_var);
                    free(literal.data);
                    free(stack.items);
                    if (err) {
                        snprintf(err->message, sizeof(err->message), "esperado 'in' em %%{for}");
                        err->line = s.line;
                        err->col = s.col;
                    }
                    return NULL;
                }
                free(in_kw);
                cl_tpl_skip_ws(&s);
                int coll_line = s.line, coll_col = s.col;
                cl_error_t sub_err = {0};
                char *span = cl_tpl_extract_span(&s, &sub_err);
                if (!span) {
                    free(key_var);
                    free(val_var);
                    free(literal.data);
                    free(stack.items);
                    cl_tpl_fail(&sub_err, err, coll_line, coll_col, "colecao invalida em %{for}");
                    return NULL;
                }
                int trim_right = cl_tpl_strip_trailing_tilde(span);
                cl_expr_t *collection = cl_tpl_parse_span(doc, span, coll_line, coll_col, &sub_err);
                if (!collection) {
                    free(key_var);
                    free(val_var);
                    free(literal.data);
                    free(stack.items);
                    cl_tpl_fail(&sub_err, err, coll_line, coll_col, "colecao invalida em %{for}");
                    return NULL;
                }
                cl_tpl_frame_t frame = {0};
                frame.is_for = 1;
                frame.for_key_var = key_var; /* freed once the matching %{endfor} consumes it */
                frame.for_val_var = val_var;
                frame.for_collection = collection;
                frame.for_body = cl_template_new(doc);
                cl_tpl_stack_push(&stack, frame);
                if (trim_right) {
                    cl_tpl_skip_ws(&s);
                }
            } else if (strcmp(keyword, "endfor") == 0) {
                free(keyword);
                if (cl_tpl_close_bare_directive(&s) < 0) {
                    free(literal.data);
                    free(stack.items);
                    if (err) {
                        snprintf(err->message, sizeof(err->message), "%%{endfor} mal formado");
                        err->line = s.line;
                        err->col = s.col;
                    }
                    return NULL;
                }
                if (stack.count == 0 || !stack.items[stack.count - 1].is_for) {
                    free(literal.data);
                    free(stack.items);
                    if (err) {
                        snprintf(err->message, sizeof(err->message), "%%{endfor} inesperado");
                        err->line = dir_line;
                        err->col = dir_col;
                    }
                    return NULL;
                }
                cl_tpl_frame_t frame = stack.items[--stack.count];
                cl_template_add_for(doc, cl_tpl_current_target(root, &stack), frame.for_key_var,
                                     frame.for_val_var, frame.for_collection, frame.for_body);
                free(frame.for_key_var);
                free(frame.for_val_var);
            } else {
                if (err) {
                    snprintf(err->message, sizeof(err->message), "diretiva de template desconhecida: %%{%s}",
                             keyword);
                    err->line = dir_line;
                    err->col = dir_col;
                }
                free(keyword);
                free(literal.data);
                free(stack.items);
                return NULL;
            }
            continue;
        }

        cl_strbuf_push(&literal, (char)cl_tpl_peek(&s, 0));
        cl_tpl_advance(&s);
    }

    cl_tpl_flush_literal(doc, root, &stack, &literal);
    free(literal.data);

    if (stack.count > 0) {
        free(stack.items);
        if (err) {
            snprintf(err->message, sizeof(err->message), "%s sem fechamento",
                     stack.items[stack.count - 1].is_for ? "%{for}" : "%{if}");
            err->line = base_line;
            err->col = base_col;
        }
        return NULL;
    }
    free(stack.items);

    if (root->count == 1 && root->parts[0].kind == CL_TPL_LITERAL) {
        return cl_expr_new_string(doc, root->parts[0].text, base_line, base_col);
    }
    if (root->count == 0) {
        return cl_expr_new_string(doc, "", base_line, base_col);
    }
    return cl_expr_new_template(doc, root, base_line, base_col);
}
