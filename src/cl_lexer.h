#ifndef CL_LEXER_H
#define CL_LEXER_H

#include "cl_internal.h"

typedef enum cl_token_kind {
    CL_TOK_EOF,
    CL_TOK_NEWLINE,
    CL_TOK_IDENT,
    CL_TOK_STRING,
    CL_TOK_HEREDOC,
    CL_TOK_NUMBER,
    CL_TOK_LBRACE,
    CL_TOK_RBRACE,
    CL_TOK_LBRACKET,
    CL_TOK_RBRACKET,
    CL_TOK_LPAREN,
    CL_TOK_RPAREN,
    CL_TOK_DOT,
    CL_TOK_COMMA,
    CL_TOK_EQUAL,
    CL_TOK_COLON,
    CL_TOK_PLUS,
    CL_TOK_MINUS,
    CL_TOK_STAR,
    CL_TOK_SLASH,
    CL_TOK_PERCENT,
    CL_TOK_BANG,
    CL_TOK_EQEQ,
    CL_TOK_NEQ,
    CL_TOK_LT,
    CL_TOK_LE,
    CL_TOK_GT,
    CL_TOK_GE,
    CL_TOK_ANDAND,
    CL_TOK_OROR,
    CL_TOK_QUESTION,
    CL_TOK_ELLIPSIS,
    CL_TOK_FATARROW
} cl_token_kind_t;

typedef struct cl_token {
    cl_token_kind_t kind;
    const char *text;  /* arena-owned, NUL-terminated; set for IDENT/STRING/HEREDOC */
    double number;      /* set for NUMBER */
    int line;
    int col;
} cl_token_t;

/* Tokenizes `source` in full. On success returns 0, sets *out_tokens to a
 * malloc'd array (caller must free() it, not the arena) terminated by a
 * CL_TOK_EOF token, and *out_count to its length (including that EOF
 * token). On failure returns -1 and fills *err. */
int cl_lexer_tokenize(cl_document_t *doc, const char *source, cl_token_t **out_tokens,
                       size_t *out_count, cl_error_t *err);

#endif /* CL_LEXER_H */
