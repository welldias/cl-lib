#include "cl_lexer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    cl_document_t *doc;
    const char *src;
    size_t pos;
    size_t len;
    int line;
    int col;
    int bracket_depth;

    cl_token_t *tokens;
    size_t token_count;
    size_t token_capacity;
} cl_lexer_t;

static void cl_lexer_push(cl_lexer_t *lx, cl_token_kind_t kind, const char *text, double number,
                           int line, int col) {
    if (lx->token_count == lx->token_capacity) {
        size_t new_capacity = lx->token_capacity ? lx->token_capacity * 2 : 64;
        cl_token_t *new_tokens = realloc(lx->tokens, new_capacity * sizeof(cl_token_t));
        if (!new_tokens) {
            abort();
        }
        lx->tokens = new_tokens;
        lx->token_capacity = new_capacity;
    }
    cl_token_t *tok = &lx->tokens[lx->token_count++];
    tok->kind = kind;
    tok->text = text;
    tok->number = number;
    tok->line = line;
    tok->col = col;
}

static int cl_lexer_error(cl_error_t *err, int line, int col, const char *message) {
    if (err) {
        snprintf(err->message, sizeof(err->message), "%s", message);
        err->line = line;
        err->col = col;
    }
    return -1;
}

static int cl_peek(const cl_lexer_t *lx, size_t offset) {
    size_t at = lx->pos + offset;
    if (at >= lx->len) {
        return '\0';
    }
    return (unsigned char)lx->src[at];
}

static void cl_advance(cl_lexer_t *lx) {
    if (lx->src[lx->pos] == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    lx->pos++;
}

static int cl_is_ident_start(int c) {
    return isalpha(c) || c == '_';
}

static int cl_is_ident_continue(int c) {
    return isalnum(c) || c == '_' || c == '-';
}

/* Small growable buffer used while decoding a quoted string or heredoc;
 * freed as soon as the final value has been copied into the document
 * arena. */
typedef struct {
    char *data;
    size_t len;
    size_t capacity;
} cl_strbuf_t;

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

static int cl_lex_string(cl_lexer_t *lx, cl_error_t *err) {
    int start_line = lx->line;
    int start_col = lx->col;
    cl_advance(lx); /* opening quote */

    cl_strbuf_t buf = {0};
    for (;;) {
        int c = cl_peek(lx, 0);
        if (c == '\0' || c == '\n') {
            free(buf.data);
            return cl_lexer_error(err, start_line, start_col, "string sem fechamento");
        }
        if (c == '"') {
            cl_advance(lx);
            break;
        }
        if (c == '\\') {
            cl_advance(lx);
            int esc = cl_peek(lx, 0);
            char decoded;
            switch (esc) {
                case '"': decoded = '"'; break;
                case '\\': decoded = '\\'; break;
                case '/': decoded = '/'; break;
                case 'n': decoded = '\n'; break;
                case 't': decoded = '\t'; break;
                case 'r': decoded = '\r'; break;
                default:
                    free(buf.data);
                    return cl_lexer_error(err, lx->line, lx->col, "sequencia de escape invalida");
            }
            cl_strbuf_push(&buf, decoded);
            cl_advance(lx);
            continue;
        }
        if ((c == '$' || c == '%') && cl_peek(lx, 1) == '{') {
            /* Copy the "${...}"/"%{...}" span through verbatim (raw, not
             * escape-decoded) so cl_compile_template can re-lex it later.
             * This string token's own closing quote must not be confused
             * by a quoted string literal nested inside that span (e.g.
             * "${upper(\"x\")}"), so braces and nested strings here are
             * tracked/skipped without interpreting them. */
            cl_strbuf_push(&buf, (char)c);
            cl_advance(lx);
            cl_strbuf_push(&buf, '{');
            cl_advance(lx);
            int depth = 1;
            while (depth > 0) {
                int ic = cl_peek(lx, 0);
                if (ic == '\0') {
                    free(buf.data);
                    return cl_lexer_error(err, start_line, start_col, "string sem fechamento");
                }
                if (ic == '"') {
                    cl_strbuf_push(&buf, '"');
                    cl_advance(lx);
                    while (cl_peek(lx, 0) != '\0' && cl_peek(lx, 0) != '"') {
                        if (cl_peek(lx, 0) == '\\' && cl_peek(lx, 1) != '\0') {
                            cl_strbuf_push(&buf, (char)cl_peek(lx, 0));
                            cl_advance(lx);
                        }
                        cl_strbuf_push(&buf, (char)cl_peek(lx, 0));
                        cl_advance(lx);
                    }
                    if (cl_peek(lx, 0) == '"') {
                        cl_strbuf_push(&buf, '"');
                        cl_advance(lx);
                    }
                    continue;
                }
                if (ic == '{') {
                    depth++;
                } else if (ic == '}') {
                    depth--;
                }
                cl_strbuf_push(&buf, (char)ic);
                cl_advance(lx);
            }
            continue;
        }
        cl_strbuf_push(&buf, (char)c);
        cl_advance(lx);
    }
    cl_strbuf_push(&buf, '\0');

    char *text = cl_arena_strdup(lx->doc, buf.data);
    free(buf.data);
    cl_lexer_push(lx, CL_TOK_STRING, text, 0.0, start_line, start_col);
    return 0;
}

/* Reads "<<[-]MARKER\n ... \nMARKER" (the opening "<<" is already known to
 * be there, but not yet consumed). Content is returned raw except that,
 * for "<<-", every line has up to as many leading spaces/tabs stripped as
 * the closing marker line itself was indented by. The closing marker line
 * is always allowed to carry leading whitespace (a small, documented
 * leniency beyond the strict HCL spec, needed to detect the end of the
 * heredoc regardless of the "-" flag) - only the dedent step itself is
 * conditional on "-". */
static int cl_lex_heredoc(cl_lexer_t *lx, cl_error_t *err) {
    int start_line = lx->line;
    int start_col = lx->col;
    cl_advance(lx); /* first '<' */
    cl_advance(lx); /* second '<' */

    int indented = 0;
    if (cl_peek(lx, 0) == '-') {
        indented = 1;
        cl_advance(lx);
    }

    if (!cl_is_ident_start(cl_peek(lx, 0))) {
        return cl_lexer_error(err, start_line, start_col, "marcador de heredoc invalido");
    }
    size_t marker_start = lx->pos;
    cl_advance(lx);
    while (cl_is_ident_continue(cl_peek(lx, 0))) {
        cl_advance(lx);
    }
    size_t marker_len = lx->pos - marker_start;
    char marker[128];
    if (marker_len >= sizeof(marker)) {
        return cl_lexer_error(err, start_line, start_col, "marcador de heredoc muito longo");
    }
    memcpy(marker, lx->src + marker_start, marker_len);
    marker[marker_len] = '\0';

    while (cl_peek(lx, 0) == ' ' || cl_peek(lx, 0) == '\t' || cl_peek(lx, 0) == '\r') {
        cl_advance(lx);
    }
    if (cl_peek(lx, 0) != '\n') {
        return cl_lexer_error(err, lx->line, lx->col, "conteudo inesperado apos o marcador de heredoc");
    }
    cl_advance(lx); /* newline right after the marker */

    char **lines = NULL;
    size_t line_count = 0;
    size_t line_capacity = 0;
    size_t closing_indent = 0;

    for (;;) {
        if (cl_peek(lx, 0) == '\0') {
            for (size_t i = 0; i < line_count; i++) {
                free(lines[i]);
            }
            free(lines);
            return cl_lexer_error(err, start_line, start_col, "heredoc sem fechamento");
        }

        size_t line_start = lx->pos;
        while (cl_peek(lx, 0) != '\0' && cl_peek(lx, 0) != '\n') {
            cl_advance(lx);
        }
        size_t line_len = lx->pos - line_start;
        if (line_len > 0 && lx->src[line_start + line_len - 1] == '\r') {
            line_len--; /* tolerate CRLF line endings inside the heredoc body */
        }
        if (cl_peek(lx, 0) == '\n') {
            cl_advance(lx);
        }

        size_t leading = 0;
        while (leading < line_len &&
               (lx->src[line_start + leading] == ' ' || lx->src[line_start + leading] == '\t')) {
            leading++;
        }
        size_t rest_len = line_len - leading;
        if (rest_len == marker_len && memcmp(lx->src + line_start + leading, marker, marker_len) == 0) {
            closing_indent = leading;
            break;
        }

        char *line_copy = malloc(line_len + 1);
        if (!line_copy) {
            abort();
        }
        memcpy(line_copy, lx->src + line_start, line_len);
        line_copy[line_len] = '\0';

        if (line_count == line_capacity) {
            size_t new_capacity = line_capacity ? line_capacity * 2 : 16;
            char **new_lines = realloc(lines, new_capacity * sizeof(char *));
            if (!new_lines) {
                abort();
            }
            lines = new_lines;
            line_capacity = new_capacity;
        }
        lines[line_count++] = line_copy;
    }

    cl_strbuf_t buf = {0};
    for (size_t i = 0; i < line_count; i++) {
        char *line = lines[i];
        size_t len = strlen(line);
        size_t strip = 0;
        if (indented) {
            while (strip < len && strip < closing_indent && (line[strip] == ' ' || line[strip] == '\t')) {
                strip++;
            }
        }
        for (size_t j = strip; j < len; j++) {
            cl_strbuf_push(&buf, line[j]);
        }
        cl_strbuf_push(&buf, '\n');
        free(line);
    }
    free(lines);
    cl_strbuf_push(&buf, '\0');

    char *text = cl_arena_strdup(lx->doc, buf.data ? buf.data : "");
    free(buf.data);
    cl_lexer_push(lx, CL_TOK_HEREDOC, text, 0.0, start_line, start_col);

    /* The closing marker line's own newline was already consumed above
     * without going through the main loop's newline-token logic. Emit it
     * here so a heredoc still ends its statement like any other value,
     * unless we are inside "(" / "[" where newlines are suppressed. */
    if (lx->bracket_depth == 0) {
        cl_lexer_push(lx, CL_TOK_NEWLINE, NULL, 0.0, lx->line, lx->col);
    }
    return 0;
}

static void cl_lex_number(cl_lexer_t *lx) {
    int start_line = lx->line;
    int start_col = lx->col;
    size_t start = lx->pos;

    while (isdigit(cl_peek(lx, 0))) {
        cl_advance(lx);
    }
    if (cl_peek(lx, 0) == '.' && isdigit(cl_peek(lx, 1))) {
        cl_advance(lx);
        while (isdigit(cl_peek(lx, 0))) {
            cl_advance(lx);
        }
    }
    if (cl_peek(lx, 0) == 'e' || cl_peek(lx, 0) == 'E') {
        size_t save_pos = lx->pos;
        int save_line = lx->line;
        int save_col = lx->col;
        cl_advance(lx);
        if (cl_peek(lx, 0) == '+' || cl_peek(lx, 0) == '-') {
            cl_advance(lx);
        }
        if (isdigit(cl_peek(lx, 0))) {
            while (isdigit(cl_peek(lx, 0))) {
                cl_advance(lx);
            }
        } else {
            lx->pos = save_pos;
            lx->line = save_line;
            lx->col = save_col;
        }
    }

    char text[64];
    size_t n = lx->pos - start;
    if (n >= sizeof(text)) {
        n = sizeof(text) - 1;
    }
    memcpy(text, lx->src + start, n);
    text[n] = '\0';
    double value = strtod(text, NULL);
    cl_lexer_push(lx, CL_TOK_NUMBER, NULL, value, start_line, start_col);
}

static void cl_lex_ident(cl_lexer_t *lx) {
    int start_line = lx->line;
    int start_col = lx->col;
    size_t start = lx->pos;

    cl_advance(lx);
    while (cl_is_ident_continue(cl_peek(lx, 0))) {
        cl_advance(lx);
    }

    char *text = cl_arena_strndup(lx->doc, lx->src + start, lx->pos - start);
    cl_lexer_push(lx, CL_TOK_IDENT, text, 0.0, start_line, start_col);
}

int cl_lexer_tokenize(cl_document_t *doc, const char *source, cl_token_t **out_tokens,
                       size_t *out_count, cl_error_t *err) {
    cl_lexer_t lx = {0};
    lx.doc = doc;
    lx.src = source;
    lx.len = strlen(source);
    lx.line = 1;
    lx.col = 1;

    for (;;) {
        int c = cl_peek(&lx, 0);

        if (c == '\0') {
            break;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            cl_advance(&lx);
            continue;
        }
        if (c == '\n') {
            cl_advance(&lx);
            if (lx.bracket_depth == 0 && lx.token_count > 0 &&
                lx.tokens[lx.token_count - 1].kind != CL_TOK_NEWLINE) {
                cl_lexer_push(&lx, CL_TOK_NEWLINE, NULL, 0.0, lx.line, lx.col);
            }
            continue;
        }
        if (c == '#') {
            while (cl_peek(&lx, 0) != '\0' && cl_peek(&lx, 0) != '\n') {
                cl_advance(&lx);
            }
            continue;
        }
        if (c == '/' && cl_peek(&lx, 1) == '/') {
            while (cl_peek(&lx, 0) != '\0' && cl_peek(&lx, 0) != '\n') {
                cl_advance(&lx);
            }
            continue;
        }
        if (c == '/' && cl_peek(&lx, 1) == '*') {
            int start_line = lx.line;
            int start_col = lx.col;
            cl_advance(&lx);
            cl_advance(&lx);
            while (!(cl_peek(&lx, 0) == '*' && cl_peek(&lx, 1) == '/')) {
                if (cl_peek(&lx, 0) == '\0') {
                    free(lx.tokens);
                    return cl_lexer_error(err, start_line, start_col, "comentario de bloco sem fechamento");
                }
                cl_advance(&lx);
            }
            cl_advance(&lx);
            cl_advance(&lx);
            continue;
        }
        if (c == '<' && cl_peek(&lx, 1) == '<') {
            if (cl_lex_heredoc(&lx, err) != 0) {
                free(lx.tokens);
                return -1;
            }
            continue;
        }
        if (c == '"') {
            if (cl_lex_string(&lx, err) != 0) {
                free(lx.tokens);
                return -1;
            }
            continue;
        }
        if (isdigit(c)) {
            cl_lex_number(&lx);
            continue;
        }
        if (cl_is_ident_start(c)) {
            cl_lex_ident(&lx);
            continue;
        }

        int line = lx.line;
        int col = lx.col;
        switch (c) {
            case '{': cl_advance(&lx); cl_lexer_push(&lx, CL_TOK_LBRACE, NULL, 0.0, line, col); break;
            case '}': cl_advance(&lx); cl_lexer_push(&lx, CL_TOK_RBRACE, NULL, 0.0, line, col); break;
            case '[': cl_advance(&lx); lx.bracket_depth++; cl_lexer_push(&lx, CL_TOK_LBRACKET, NULL, 0.0, line, col); break;
            case ']':
                cl_advance(&lx);
                if (lx.bracket_depth > 0) {
                    lx.bracket_depth--;
                }
                cl_lexer_push(&lx, CL_TOK_RBRACKET, NULL, 0.0, line, col);
                break;
            case '(': cl_advance(&lx); lx.bracket_depth++; cl_lexer_push(&lx, CL_TOK_LPAREN, NULL, 0.0, line, col); break;
            case ')':
                cl_advance(&lx);
                if (lx.bracket_depth > 0) {
                    lx.bracket_depth--;
                }
                cl_lexer_push(&lx, CL_TOK_RPAREN, NULL, 0.0, line, col);
                break;
            case '.':
                cl_advance(&lx);
                if (cl_peek(&lx, 0) == '.' && cl_peek(&lx, 1) == '.') {
                    cl_advance(&lx);
                    cl_advance(&lx);
                    cl_lexer_push(&lx, CL_TOK_ELLIPSIS, NULL, 0.0, line, col);
                } else {
                    cl_lexer_push(&lx, CL_TOK_DOT, NULL, 0.0, line, col);
                }
                break;
            case ',': cl_advance(&lx); cl_lexer_push(&lx, CL_TOK_COMMA, NULL, 0.0, line, col); break;
            case '=':
                cl_advance(&lx);
                if (cl_peek(&lx, 0) == '=') {
                    cl_advance(&lx);
                    cl_lexer_push(&lx, CL_TOK_EQEQ, NULL, 0.0, line, col);
                } else if (cl_peek(&lx, 0) == '>') {
                    cl_advance(&lx);
                    cl_lexer_push(&lx, CL_TOK_FATARROW, NULL, 0.0, line, col);
                } else {
                    cl_lexer_push(&lx, CL_TOK_EQUAL, NULL, 0.0, line, col);
                }
                break;
            case ':': cl_advance(&lx); cl_lexer_push(&lx, CL_TOK_COLON, NULL, 0.0, line, col); break;
            case '+': cl_advance(&lx); cl_lexer_push(&lx, CL_TOK_PLUS, NULL, 0.0, line, col); break;
            case '-': cl_advance(&lx); cl_lexer_push(&lx, CL_TOK_MINUS, NULL, 0.0, line, col); break;
            case '*': cl_advance(&lx); cl_lexer_push(&lx, CL_TOK_STAR, NULL, 0.0, line, col); break;
            case '/': cl_advance(&lx); cl_lexer_push(&lx, CL_TOK_SLASH, NULL, 0.0, line, col); break;
            case '%': cl_advance(&lx); cl_lexer_push(&lx, CL_TOK_PERCENT, NULL, 0.0, line, col); break;
            case '!':
                cl_advance(&lx);
                if (cl_peek(&lx, 0) == '=') {
                    cl_advance(&lx);
                    cl_lexer_push(&lx, CL_TOK_NEQ, NULL, 0.0, line, col);
                } else {
                    cl_lexer_push(&lx, CL_TOK_BANG, NULL, 0.0, line, col);
                }
                break;
            case '<':
                cl_advance(&lx);
                if (cl_peek(&lx, 0) == '=') {
                    cl_advance(&lx);
                    cl_lexer_push(&lx, CL_TOK_LE, NULL, 0.0, line, col);
                } else {
                    cl_lexer_push(&lx, CL_TOK_LT, NULL, 0.0, line, col);
                }
                break;
            case '>':
                cl_advance(&lx);
                if (cl_peek(&lx, 0) == '=') {
                    cl_advance(&lx);
                    cl_lexer_push(&lx, CL_TOK_GE, NULL, 0.0, line, col);
                } else {
                    cl_lexer_push(&lx, CL_TOK_GT, NULL, 0.0, line, col);
                }
                break;
            case '&':
                if (cl_peek(&lx, 1) == '&') {
                    cl_advance(&lx);
                    cl_advance(&lx);
                    cl_lexer_push(&lx, CL_TOK_ANDAND, NULL, 0.0, line, col);
                } else {
                    free(lx.tokens);
                    return cl_lexer_error(err, line, col, "caractere inesperado");
                }
                break;
            case '|':
                if (cl_peek(&lx, 1) == '|') {
                    cl_advance(&lx);
                    cl_advance(&lx);
                    cl_lexer_push(&lx, CL_TOK_OROR, NULL, 0.0, line, col);
                } else {
                    free(lx.tokens);
                    return cl_lexer_error(err, line, col, "caractere inesperado");
                }
                break;
            case '?': cl_advance(&lx); cl_lexer_push(&lx, CL_TOK_QUESTION, NULL, 0.0, line, col); break;
            default: {
                free(lx.tokens);
                return cl_lexer_error(err, line, col, "caractere inesperado");
            }
        }
    }

    cl_lexer_push(&lx, CL_TOK_EOF, NULL, 0.0, lx.line, lx.col);
    *out_tokens = lx.tokens;
    *out_count = lx.token_count;
    return 0;
}
