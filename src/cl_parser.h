#ifndef CL_PARSER_H
#define CL_PARSER_H

#include "cl_internal.h"

/* Parses `source` and, on success, sets doc->root and returns 0. On
 * failure returns -1 and fills *err with a line/col-anchored message;
 * doc->root is left untouched. */
int cl_parser_parse(cl_document_t *doc, const char *source, cl_error_t *err);

/* Parses a standalone expression out of `expr_text` (e.g. the inside of a
 * "${...}" interpolation or a "%{if ...}" clause). base_line/base_col
 * locate expr_text within the real source file and are used to translate
 * any error position back to real file coordinates. Returns NULL and
 * fills *err (line/col already translated) on failure, including when
 * trailing tokens remain after a complete expression. */
cl_expr_t *cl_parser_parse_expr_string(cl_document_t *doc, const char *expr_text, int base_line, int base_col,
                                        cl_error_t *err);

#endif /* CL_PARSER_H */
