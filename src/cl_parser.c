#include "cl_parser.h"

#include "cl_ast.h"
#include "cl_lexer.h"
#include "cl_template.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    cl_document_t *doc;
    cl_token_t *toks;
    size_t count;
    size_t pos;
    cl_error_t *err;
    int failed;
} cl_parser_state_t;

static const cl_token_t *cl_cur(cl_parser_state_t *state) {
    return &state->toks[state->pos];
}

static const cl_token_t *cl_peek_tok(cl_parser_state_t *state, size_t offset) {
    size_t idx = state->pos + offset;
    if (idx >= state->count) {
        idx = state->count - 1; /* EOF is always the last token */
    }
    return &state->toks[idx];
}

static void cl_advance_token(cl_parser_state_t *state) {
    if (state->pos + 1 < state->count) {
        state->pos++;
    }
}

static void cl_fail(cl_parser_state_t *state, const cl_token_t *tok, const char *message) {
    if (state->failed) {
        return; /* keep the first error */
    }
    state->failed = 1;
    if (state->err) {
        snprintf(state->err->message, sizeof(state->err->message), "%s", message);
        state->err->line = tok->line;
        state->err->col = tok->col;
    }
}

static void cl_skip_newlines(cl_parser_state_t *state) {
    while (cl_cur(state)->kind == CL_TOK_NEWLINE) {
        cl_advance_token(state);
    }
}

static cl_expr_t *cl_parse_expr(cl_parser_state_t *state);

/* ------------------------------------------------------------------ */
/* Primary expressions                                                  */
/* ------------------------------------------------------------------ */

/* Parses the inside of a "[...]" traversal step, with the '[' already
 * consumed, through the closing ']'. A lone number or string literal keeps
 * producing CL_STEP_INDEX_NUMBER/CL_STEP_INDEX_STRING exactly as before (so
 * the writer, round-trips and the block-label resolution in cl_eval.c see
 * no difference); anything else - an identifier, an interpolated string, an
 * operator, a call - becomes a CL_STEP_INDEX_EXPR evaluated at eval time.
 * Only fills the fields of `out` that its kind uses; `out->name` points at
 * token/AST text the caller copies. Returns 0 on success. */
static int cl_parse_index_step(cl_parser_state_t *state, cl_traversal_step_t *out) {
    const cl_token_t *index_tok = cl_cur(state);
    int lone_literal = cl_peek_tok(state, 1)->kind == CL_TOK_RBRACKET;

    if (lone_literal && index_tok->kind == CL_TOK_NUMBER) {
        cl_advance_token(state);
        out->kind = CL_STEP_INDEX_NUMBER;
        out->index = index_tok->number;
    } else {
        cl_expr_t *index = cl_parse_expr(state);
        if (state->failed) {
            return -1;
        }
        if (lone_literal && index->kind == CL_EXPR_STRING) {
            out->kind = CL_STEP_INDEX_STRING;
            out->name = index->as.string_value;
        } else {
            out->kind = CL_STEP_INDEX_EXPR;
            out->expr = index;
        }
    }

    const cl_token_t *close_tok = cl_cur(state);
    if (close_tok->kind != CL_TOK_RBRACKET) {
        cl_fail(state, close_tok, "expected ']'");
        return -1;
    }
    cl_advance_token(state);
    return 0;
}

static cl_expr_t *cl_parse_traversal(cl_parser_state_t *state) {
    const cl_token_t *root_tok = cl_cur(state);
    cl_advance_token(state);
    cl_expr_t *expr = cl_expr_new_traversal(state->doc, root_tok->text, root_tok->line, root_tok->col);

    for (;;) {
        const cl_token_t *tok = cl_cur(state);
        if (tok->kind == CL_TOK_DOT) {
            cl_advance_token(state);
            if (cl_cur(state)->kind == CL_TOK_STAR) {
                cl_advance_token(state);
                cl_expr_traversal_add_splat_attr(state->doc, expr);
                continue;
            }
            const cl_token_t *name_tok = cl_cur(state);
            if (name_tok->kind != CL_TOK_IDENT) {
                cl_fail(state, name_tok, "expected identifier after '.'");
                return NULL;
            }
            cl_advance_token(state);
            cl_expr_traversal_add_attr(state->doc, expr, name_tok->text);
        } else if (tok->kind == CL_TOK_LBRACKET) {
            cl_advance_token(state);
            if (cl_cur(state)->kind == CL_TOK_STAR && cl_peek_tok(state, 1)->kind == CL_TOK_RBRACKET) {
                cl_advance_token(state); /* '*' */
                cl_advance_token(state); /* ']' */
                cl_expr_traversal_add_splat_full(state->doc, expr);
                continue;
            }
            cl_traversal_step_t step;
            if (cl_parse_index_step(state, &step) != 0) {
                return NULL;
            }
            if (step.kind == CL_STEP_INDEX_STRING) {
                cl_expr_traversal_add_index_string(state->doc, expr, step.name);
            } else if (step.kind == CL_STEP_INDEX_NUMBER) {
                cl_expr_traversal_add_index_number(state->doc, expr, step.index);
            } else {
                cl_expr_traversal_add_index_expr(state->doc, expr, step.expr);
            }
        } else {
            break;
        }
    }
    return expr;
}

/* Consumes any trailing ".attr" / ".*" / "[idx]" / "[*]" steps after a
 * primary expression that isn't a bare identifier (a call, a parenthesized
 * expression, an object/tuple literal, or a for-expression). Identifier
 * roots don't go through here: cl_parse_traversal() already consumes its
 * own postfix chain inline, using CL_EXPR_TRAVERSAL directly so the
 * "var.x" resolution algorithm in cl_eval.c keeps working exactly as
 * before. This function only wraps `base` in a CL_EXPR_POSTFIX node lazily,
 * the first time it actually finds something to consume, so a primary with
 * no trailing steps is returned unchanged. */
static cl_expr_t *cl_parse_postfix_steps(cl_parser_state_t *state, cl_expr_t *base) {
    cl_expr_t *expr = NULL;

    for (;;) {
        const cl_token_t *tok = cl_cur(state);
        if (tok->kind == CL_TOK_DOT) {
            cl_advance_token(state);
            if (!expr) {
                expr = cl_expr_new_postfix(state->doc, base, base->line, base->col);
            }
            if (cl_cur(state)->kind == CL_TOK_STAR) {
                cl_advance_token(state);
                cl_expr_postfix_add_splat_attr(state->doc, expr);
                continue;
            }
            const cl_token_t *name_tok = cl_cur(state);
            if (name_tok->kind != CL_TOK_IDENT) {
                cl_fail(state, name_tok, "expected identifier after '.'");
                return NULL;
            }
            cl_advance_token(state);
            cl_expr_postfix_add_attr(state->doc, expr, name_tok->text);
        } else if (tok->kind == CL_TOK_LBRACKET) {
            cl_advance_token(state);
            if (!expr) {
                expr = cl_expr_new_postfix(state->doc, base, base->line, base->col);
            }
            if (cl_cur(state)->kind == CL_TOK_STAR && cl_peek_tok(state, 1)->kind == CL_TOK_RBRACKET) {
                cl_advance_token(state); /* '*' */
                cl_advance_token(state); /* ']' */
                cl_expr_postfix_add_splat_full(state->doc, expr);
                continue;
            }
            cl_traversal_step_t step;
            if (cl_parse_index_step(state, &step) != 0) {
                return NULL;
            }
            if (step.kind == CL_STEP_INDEX_STRING) {
                cl_expr_postfix_add_index_string(state->doc, expr, step.name);
            } else if (step.kind == CL_STEP_INDEX_NUMBER) {
                cl_expr_postfix_add_index_number(state->doc, expr, step.index);
            } else {
                cl_expr_postfix_add_index_expr(state->doc, expr, step.expr);
            }
        } else {
            break;
        }
    }

    return expr ? expr : base;
}

static cl_expr_t *cl_parse_call(cl_parser_state_t *state) {
    const cl_token_t *name_tok = cl_cur(state);
    cl_advance_token(state); /* IDENT */
    cl_advance_token(state); /* '(' */

    cl_expr_t *call = cl_expr_new_call(state->doc, name_tok->text, 0, name_tok->line, name_tok->col);

    while (cl_cur(state)->kind != CL_TOK_RPAREN) {
        if (cl_cur(state)->kind == CL_TOK_EOF) {
            cl_fail(state, cl_cur(state), "expected ')'");
            return NULL;
        }
        cl_expr_t *arg = cl_parse_expr(state);
        if (state->failed) {
            return NULL;
        }
        cl_expr_call_add_arg(state->doc, call, arg);

        if (cl_cur(state)->kind == CL_TOK_ELLIPSIS) {
            cl_advance_token(state);
            call->as.call.expand_final = 1;
            if (cl_cur(state)->kind == CL_TOK_COMMA) {
                cl_advance_token(state);
            }
            break;
        }
        if (cl_cur(state)->kind == CL_TOK_COMMA) {
            cl_advance_token(state);
            continue;
        }
        break;
    }

    if (cl_cur(state)->kind != CL_TOK_RPAREN) {
        cl_fail(state, cl_cur(state), "expected ')'");
        return NULL;
    }
    cl_advance_token(state);
    return call;
}

static cl_expr_t *cl_parse_for(cl_parser_state_t *state, const cl_token_t *open_tok, int is_object) {
    cl_advance_token(state); /* 'for' */

    const cl_token_t *var1_tok = cl_cur(state);
    if (var1_tok->kind != CL_TOK_IDENT) {
        cl_fail(state, var1_tok, "expected variable in for-expression");
        return NULL;
    }
    cl_advance_token(state);

    const char *key_var = NULL;
    const char *val_var = var1_tok->text;

    if (cl_cur(state)->kind == CL_TOK_COMMA) {
        cl_advance_token(state);
        const cl_token_t *var2_tok = cl_cur(state);
        if (var2_tok->kind != CL_TOK_IDENT) {
            cl_fail(state, var2_tok, "expected second variable in for-expression");
            return NULL;
        }
        cl_advance_token(state);
        key_var = var1_tok->text;
        val_var = var2_tok->text;
    }

    const cl_token_t *in_tok = cl_cur(state);
    if (in_tok->kind != CL_TOK_IDENT || strcmp(in_tok->text, "in") != 0) {
        cl_fail(state, in_tok, "expected 'in' in for-expression");
        return NULL;
    }
    cl_advance_token(state);

    cl_expr_t *collection = cl_parse_expr(state);
    if (state->failed) {
        return NULL;
    }

    if (cl_cur(state)->kind != CL_TOK_COLON) {
        cl_fail(state, cl_cur(state), "expected ':' in for-expression");
        return NULL;
    }
    cl_advance_token(state);

    cl_expr_t *key_expr = NULL;
    cl_expr_t *value_expr = NULL;
    int grouping = 0;

    if (is_object) {
        key_expr = cl_parse_expr(state);
        if (state->failed) {
            return NULL;
        }
        if (cl_cur(state)->kind != CL_TOK_FATARROW) {
            cl_fail(state, cl_cur(state), "expected '=>' in object for-expression");
            return NULL;
        }
        cl_advance_token(state);
        value_expr = cl_parse_expr(state);
        if (state->failed) {
            return NULL;
        }
        if (cl_cur(state)->kind == CL_TOK_ELLIPSIS) {
            grouping = 1;
            cl_advance_token(state);
        }
    } else {
        value_expr = cl_parse_expr(state);
        if (state->failed) {
            return NULL;
        }
    }

    cl_expr_t *cond = NULL;
    if (cl_cur(state)->kind == CL_TOK_IDENT && strcmp(cl_cur(state)->text, "if") == 0) {
        cl_advance_token(state);
        cond = cl_parse_expr(state);
        if (state->failed) {
            return NULL;
        }
    }

    cl_token_kind_t closer = is_object ? CL_TOK_RBRACE : CL_TOK_RBRACKET;
    if (cl_cur(state)->kind != closer) {
        cl_fail(state, cl_cur(state), "expected end of for-expression");
        return NULL;
    }
    cl_advance_token(state);

    return cl_expr_new_for(state->doc, key_var, val_var, collection, key_expr, value_expr, cond, is_object,
                            grouping, open_tok->line, open_tok->col);
}

static cl_expr_t *cl_parse_bracket(cl_parser_state_t *state) {
    const cl_token_t *open_tok = cl_cur(state);
    cl_advance_token(state); /* '[' */

    if (cl_cur(state)->kind == CL_TOK_IDENT && strcmp(cl_cur(state)->text, "for") == 0) {
        return cl_parse_for(state, open_tok, 0);
    }

    cl_expr_t *tuple = cl_expr_new_tuple(state->doc, open_tok->line, open_tok->col);
    while (cl_cur(state)->kind != CL_TOK_RBRACKET) {
        if (cl_cur(state)->kind == CL_TOK_EOF) {
            cl_fail(state, cl_cur(state), "expected ']'");
            return NULL;
        }
        cl_expr_t *item = cl_parse_expr(state);
        if (state->failed) {
            return NULL;
        }
        cl_expr_tuple_add(state->doc, tuple, item);
        if (cl_cur(state)->kind == CL_TOK_COMMA) {
            cl_advance_token(state);
            continue;
        }
        break;
    }
    if (cl_cur(state)->kind != CL_TOK_RBRACKET) {
        cl_fail(state, cl_cur(state), "expected ']'");
        return NULL;
    }
    cl_advance_token(state);
    return tuple;
}

/* Consumes the current string/heredoc token and compiles it: every quoted
 * string is a template, wherever it appears (value, index, object key or
 * block label). The result is a plain CL_EXPR_STRING, already decoded,
 * when there is nothing to interpolate. */
static cl_expr_t *cl_parse_quoted(cl_parser_state_t *state) {
    const cl_token_t *tok = cl_cur(state);
    cl_advance_token(state);
    /* where the template text itself starts: right after the opening
     * quote, or at the start of the line after "<<MARKER" */
    int text_line = tok->kind == CL_TOK_STRING ? tok->line : tok->line + 1;
    int text_col = tok->kind == CL_TOK_STRING ? tok->col + 1 : 1;
    cl_error_t local_err = {0};
    cl_expr_t *expr = cl_compile_template(state->doc, tok->text, text_line, text_col, &local_err);
    if (!expr) {
        state->failed = 1;
        if (state->err) {
            *state->err = local_err;
        }
        return NULL;
    }
    /* the string/heredoc as a whole is still located at its token */
    expr->line = tok->line;
    expr->col = tok->col;
    return expr;
}

static cl_expr_t *cl_parse_object_body(cl_parser_state_t *state, const cl_token_t *open_tok) {
    cl_expr_t *obj = cl_expr_new_object(state->doc, open_tok->line, open_tok->col);

    cl_skip_newlines(state);
    while (cl_cur(state)->kind != CL_TOK_RBRACE) {
        const cl_token_t *key_tok = cl_cur(state);
        if (key_tok->kind == CL_TOK_EOF) {
            cl_fail(state, key_tok, "expected '}'");
            return NULL;
        }
        const char *key = NULL;    /* constant key */
        cl_expr_t *key_expr = NULL; /* computed key */
        if (key_tok->kind == CL_TOK_IDENT) {
            key = key_tok->text;
            cl_advance_token(state);
        } else if (key_tok->kind == CL_TOK_STRING) {
            key_expr = cl_parse_quoted(state);
            if (state->failed) {
                return NULL;
            }
        } else if (key_tok->kind == CL_TOK_LPAREN) {
            cl_advance_token(state);
            key_expr = cl_parse_expr(state);
            if (state->failed) {
                return NULL;
            }
            if (cl_cur(state)->kind != CL_TOK_RPAREN) {
                cl_fail(state, cl_cur(state), "expected ')'");
                return NULL;
            }
            cl_advance_token(state);
        } else {
            cl_fail(state, key_tok, "invalid object key");
            return NULL;
        }
        if (key_expr && key_expr->kind == CL_EXPR_STRING) {
            key = key_expr->as.string_value; /* nothing to compute */
            key_expr = NULL;
        }

        const cl_token_t *sep_tok = cl_cur(state);
        if (sep_tok->kind != CL_TOK_EQUAL && sep_tok->kind != CL_TOK_COLON) {
            cl_fail(state, sep_tok, "expected '=' or ':'");
            return NULL;
        }
        cl_advance_token(state);

        cl_expr_t *value = cl_parse_expr(state);
        if (state->failed) {
            return NULL;
        }
        if (key_expr) {
            cl_expr_object_add_computed(state->doc, obj, key_expr, value);
        } else {
            cl_expr_object_add(state->doc, obj, key, value);
        }

        if (cl_cur(state)->kind == CL_TOK_COMMA) {
            cl_advance_token(state);
        }
        cl_skip_newlines(state);
    }
    cl_advance_token(state); /* '}' */
    return obj;
}

static cl_expr_t *cl_parse_brace(cl_parser_state_t *state) {
    const cl_token_t *open_tok = cl_cur(state);
    cl_advance_token(state); /* '{' */
    cl_skip_newlines(state);

    if (cl_cur(state)->kind == CL_TOK_IDENT && strcmp(cl_cur(state)->text, "for") == 0) {
        return cl_parse_for(state, open_tok, 1);
    }
    return cl_parse_object_body(state, open_tok);
}

static cl_expr_t *cl_parse_primary(cl_parser_state_t *state) {
    const cl_token_t *tok = cl_cur(state);
    switch (tok->kind) {
        case CL_TOK_STRING:
        case CL_TOK_HEREDOC:
            return cl_parse_quoted(state);
        case CL_TOK_NUMBER:
            cl_advance_token(state);
            return cl_expr_new_number(state->doc, tok->number, tok->line, tok->col);
        case CL_TOK_LPAREN: {
            cl_advance_token(state);
            cl_expr_t *inner = cl_parse_expr(state);
            if (state->failed) {
                return NULL;
            }
            if (cl_cur(state)->kind != CL_TOK_RPAREN) {
                cl_fail(state, cl_cur(state), "expected ')'");
                return NULL;
            }
            cl_advance_token(state);
            return cl_parse_postfix_steps(state, inner);
        }
        case CL_TOK_LBRACKET: {
            cl_expr_t *base = cl_parse_bracket(state);
            if (state->failed) {
                return NULL;
            }
            return cl_parse_postfix_steps(state, base);
        }
        case CL_TOK_LBRACE: {
            cl_expr_t *base = cl_parse_brace(state);
            if (state->failed) {
                return NULL;
            }
            return cl_parse_postfix_steps(state, base);
        }
        case CL_TOK_IDENT:
            if (strcmp(tok->text, "true") == 0) {
                cl_advance_token(state);
                return cl_expr_new_bool(state->doc, 1, tok->line, tok->col);
            }
            if (strcmp(tok->text, "false") == 0) {
                cl_advance_token(state);
                return cl_expr_new_bool(state->doc, 0, tok->line, tok->col);
            }
            if (strcmp(tok->text, "null") == 0) {
                cl_advance_token(state);
                return cl_expr_new_null(state->doc, tok->line, tok->col);
            }
            if (cl_peek_tok(state, 1)->kind == CL_TOK_LPAREN) {
                cl_expr_t *base = cl_parse_call(state);
                if (state->failed) {
                    return NULL;
                }
                return cl_parse_postfix_steps(state, base);
            }
            return cl_parse_traversal(state);
        default:
            cl_fail(state, tok, "invalid expression");
            return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Operator precedence cascade (lowest to highest)                      */
/* ------------------------------------------------------------------ */

static cl_expr_t *cl_parse_unary(cl_parser_state_t *state) {
    const cl_token_t *tok = cl_cur(state);
    if (tok->kind == CL_TOK_MINUS || tok->kind == CL_TOK_BANG) {
        cl_advance_token(state);
        cl_expr_t *operand = cl_parse_unary(state);
        if (state->failed) {
            return NULL;
        }
        cl_unary_op_t op = (tok->kind == CL_TOK_MINUS) ? CL_OP_NEG : CL_OP_NOT;
        return cl_expr_new_unary(state->doc, op, operand, tok->line, tok->col);
    }
    return cl_parse_primary(state);
}

static cl_expr_t *cl_parse_multiplicative(cl_parser_state_t *state) {
    cl_expr_t *left = cl_parse_unary(state);
    if (state->failed) {
        return NULL;
    }
    for (;;) {
        const cl_token_t *tok = cl_cur(state);
        cl_binary_op_t op;
        if (tok->kind == CL_TOK_STAR) {
            op = CL_OP_MUL;
        } else if (tok->kind == CL_TOK_SLASH) {
            op = CL_OP_DIV;
        } else if (tok->kind == CL_TOK_PERCENT) {
            op = CL_OP_MOD;
        } else {
            break;
        }
        cl_advance_token(state);
        cl_expr_t *right = cl_parse_unary(state);
        if (state->failed) {
            return NULL;
        }
        left = cl_expr_new_binary(state->doc, op, left, right, tok->line, tok->col);
    }
    return left;
}

static cl_expr_t *cl_parse_additive(cl_parser_state_t *state) {
    cl_expr_t *left = cl_parse_multiplicative(state);
    if (state->failed) {
        return NULL;
    }
    for (;;) {
        const cl_token_t *tok = cl_cur(state);
        cl_binary_op_t op;
        if (tok->kind == CL_TOK_PLUS) {
            op = CL_OP_ADD;
        } else if (tok->kind == CL_TOK_MINUS) {
            op = CL_OP_SUB;
        } else {
            break;
        }
        cl_advance_token(state);
        cl_expr_t *right = cl_parse_multiplicative(state);
        if (state->failed) {
            return NULL;
        }
        left = cl_expr_new_binary(state->doc, op, left, right, tok->line, tok->col);
    }
    return left;
}

static cl_expr_t *cl_parse_comparison(cl_parser_state_t *state) {
    cl_expr_t *left = cl_parse_additive(state);
    if (state->failed) {
        return NULL;
    }
    for (;;) {
        const cl_token_t *tok = cl_cur(state);
        cl_binary_op_t op;
        if (tok->kind == CL_TOK_LT) {
            op = CL_OP_LT;
        } else if (tok->kind == CL_TOK_LE) {
            op = CL_OP_LE;
        } else if (tok->kind == CL_TOK_GT) {
            op = CL_OP_GT;
        } else if (tok->kind == CL_TOK_GE) {
            op = CL_OP_GE;
        } else {
            break;
        }
        cl_advance_token(state);
        cl_expr_t *right = cl_parse_additive(state);
        if (state->failed) {
            return NULL;
        }
        left = cl_expr_new_binary(state->doc, op, left, right, tok->line, tok->col);
    }
    return left;
}

static cl_expr_t *cl_parse_equality(cl_parser_state_t *state) {
    cl_expr_t *left = cl_parse_comparison(state);
    if (state->failed) {
        return NULL;
    }
    for (;;) {
        const cl_token_t *tok = cl_cur(state);
        cl_binary_op_t op;
        if (tok->kind == CL_TOK_EQEQ) {
            op = CL_OP_EQ;
        } else if (tok->kind == CL_TOK_NEQ) {
            op = CL_OP_NEQ;
        } else {
            break;
        }
        cl_advance_token(state);
        cl_expr_t *right = cl_parse_comparison(state);
        if (state->failed) {
            return NULL;
        }
        left = cl_expr_new_binary(state->doc, op, left, right, tok->line, tok->col);
    }
    return left;
}

static cl_expr_t *cl_parse_and(cl_parser_state_t *state) {
    cl_expr_t *left = cl_parse_equality(state);
    if (state->failed) {
        return NULL;
    }
    while (cl_cur(state)->kind == CL_TOK_ANDAND) {
        const cl_token_t *tok = cl_cur(state);
        cl_advance_token(state);
        cl_expr_t *right = cl_parse_equality(state);
        if (state->failed) {
            return NULL;
        }
        left = cl_expr_new_binary(state->doc, CL_OP_AND, left, right, tok->line, tok->col);
    }
    return left;
}

static cl_expr_t *cl_parse_or(cl_parser_state_t *state) {
    cl_expr_t *left = cl_parse_and(state);
    if (state->failed) {
        return NULL;
    }
    while (cl_cur(state)->kind == CL_TOK_OROR) {
        const cl_token_t *tok = cl_cur(state);
        cl_advance_token(state);
        cl_expr_t *right = cl_parse_and(state);
        if (state->failed) {
            return NULL;
        }
        left = cl_expr_new_binary(state->doc, CL_OP_OR, left, right, tok->line, tok->col);
    }
    return left;
}

static cl_expr_t *cl_parse_expr(cl_parser_state_t *state) {
    cl_expr_t *cond = cl_parse_or(state);
    if (state->failed) {
        return NULL;
    }
    if (cl_cur(state)->kind == CL_TOK_QUESTION) {
        const cl_token_t *tok = cl_cur(state);
        cl_advance_token(state);
        cl_expr_t *then_expr = cl_parse_expr(state);
        if (state->failed) {
            return NULL;
        }
        if (cl_cur(state)->kind != CL_TOK_COLON) {
            cl_fail(state, cl_cur(state), "expected ':' in conditional expression");
            return NULL;
        }
        cl_advance_token(state);
        cl_expr_t *else_expr = cl_parse_expr(state);
        if (state->failed) {
            return NULL;
        }
        return cl_expr_new_conditional(state->doc, cond, then_expr, else_expr, tok->line, tok->col);
    }
    return cond;
}

/* ------------------------------------------------------------------ */
/* Body / block / attribute grammar                                     */
/* ------------------------------------------------------------------ */

static cl_body_t *cl_parse_body(cl_parser_state_t *state, int top_level) {
    cl_body_t *body = cl_body_new(state->doc);

    for (;;) {
        cl_skip_newlines(state);
        if (state->failed) {
            return NULL;
        }
        const cl_token_t *tok = cl_cur(state);

        if (tok->kind == CL_TOK_EOF) {
            if (!top_level) {
                cl_fail(state, tok, "expected '}'");
                return NULL;
            }
            break;
        }
        if (tok->kind == CL_TOK_RBRACE) {
            if (top_level) {
                cl_fail(state, tok, "unexpected '}'");
                return NULL;
            }
            break; /* caller consumes the '}' */
        }
        if (tok->kind != CL_TOK_IDENT) {
            cl_fail(state, tok, "expected identifier (attribute or block)");
            return NULL;
        }

        const cl_token_t *name_tok = tok;
        cl_advance_token(state);
        const cl_token_t *next_tok = cl_cur(state);

        if (next_tok->kind == CL_TOK_EQUAL) {
            cl_advance_token(state);
            cl_expr_t *value = cl_parse_expr(state);
            if (state->failed) {
                return NULL;
            }
            const cl_token_t *term_tok = cl_cur(state);
            if (term_tok->kind != CL_TOK_NEWLINE && term_tok->kind != CL_TOK_EOF &&
                term_tok->kind != CL_TOK_RBRACE) {
                cl_fail(state, term_tok, "expected end of line after attribute value");
                return NULL;
            }
            cl_body_append_attribute(state->doc, body, name_tok->text, value, name_tok->line, name_tok->col);
        } else if (next_tok->kind == CL_TOK_STRING || next_tok->kind == CL_TOK_LBRACE) {
            /* labels[i] holds a constant label; label_exprs[i] a computed
             * one. Both grow in step; label_exprs is dropped (NULL) at the
             * end when no label turned out to be computed. */
            char **labels = NULL;
            cl_expr_t **label_exprs = NULL;
            size_t label_count = 0;
            size_t label_capacity = 0;
            size_t expr_count = 0;
            size_t expr_capacity = 0;
            int any_computed = 0;
            while (cl_cur(state)->kind == CL_TOK_STRING) {
                cl_expr_t *label = cl_parse_quoted(state);
                if (state->failed) {
                    return NULL;
                }
                cl_array_grow(state->doc, (void **)&labels, &label_count, &label_capacity, sizeof(char *));
                cl_array_grow(state->doc, (void **)&label_exprs, &expr_count, &expr_capacity, sizeof(cl_expr_t *));
                if (label->kind == CL_EXPR_STRING) {
                    labels[label_count++] = label->as.string_value;
                    label_exprs[expr_count++] = NULL;
                } else {
                    labels[label_count++] = NULL;
                    label_exprs[expr_count++] = label;
                    any_computed = 1;
                }
            }
            if (!any_computed) {
                label_exprs = NULL;
            }
            const cl_token_t *brace_tok = cl_cur(state);
            if (brace_tok->kind != CL_TOK_LBRACE) {
                cl_fail(state, brace_tok, "expected '{' after block labels");
                return NULL;
            }
            cl_advance_token(state); /* '{' */
            cl_body_t *child = cl_parse_body(state, 0);
            if (state->failed) {
                return NULL;
            }
            const cl_token_t *close_tok = cl_cur(state);
            if (close_tok->kind != CL_TOK_RBRACE) {
                cl_fail(state, close_tok, "expected '}'");
                return NULL;
            }
            cl_advance_token(state); /* '}' */
            cl_body_append_block(state->doc, body, name_tok->text, labels, label_exprs, label_count, child,
                                  name_tok->line, name_tok->col);
        } else {
            cl_fail(state, next_tok, "expected '=' or block label");
            return NULL;
        }
    }

    return body;
}

/* ------------------------------------------------------------------ */
/* Entry points                                                         */
/* ------------------------------------------------------------------ */

int cl_parser_parse(cl_document_t *doc, const char *source, cl_error_t *err) {
    cl_token_t *tokens = NULL;
    size_t token_count = 0;
    if (cl_lexer_tokenize(doc, source, &tokens, &token_count, err) != 0) {
        return -1;
    }

    cl_parser_state_t state = {0};
    state.doc = doc;
    state.toks = tokens;
    state.count = token_count;
    state.pos = 0;
    state.err = err;
    state.failed = 0;

    cl_body_t *root = cl_parse_body(&state, 1);
    free(tokens);

    if (state.failed) {
        return -1;
    }
    doc->root = root;
    return 0;
}

cl_expr_t *cl_parser_parse_expr_string(cl_document_t *doc, const char *expr_text, int base_line, int base_col,
                                        cl_error_t *err) {
    cl_token_t *tokens = NULL;
    size_t token_count = 0;
    cl_error_t local_err = {0};
    if (cl_lexer_tokenize(doc, expr_text, &tokens, &token_count, &local_err) != 0) {
        if (err) {
            *err = local_err;
            int sub_line = err->line;
            err->line = base_line + (sub_line - 1);
            if (sub_line == 1) {
                err->col = base_col + (err->col - 1);
            }
        }
        return NULL;
    }

    /* `expr_text` was cut out of a larger source (a "${...}" span of a
     * template), so its tokens are positioned relative to that span. Map
     * them back to where the span sits in the file before parsing, so every
     * AST node - and every parse or evaluation error that reports one -
     * points at the real line/column. */
    for (size_t i = 0; i < token_count; i++) {
        if (tokens[i].line == 1) {
            tokens[i].col = base_col + (tokens[i].col - 1);
        }
        tokens[i].line = base_line + (tokens[i].line - 1);
    }

    cl_parser_state_t state = {0};
    state.doc = doc;
    state.toks = tokens;
    state.count = token_count;
    state.pos = 0;
    state.err = err;
    state.failed = 0;

    cl_expr_t *expr = cl_parse_expr(&state);
    if (!state.failed && cl_cur(&state)->kind != CL_TOK_EOF) {
        cl_fail(&state, cl_cur(&state), "unexpected content after expression");
    }
    free(tokens);

    if (state.failed) {
        return NULL;
    }
    return expr;
}
