#ifndef CL_TEMPLATE_H
#define CL_TEMPLATE_H

#include "cl_internal.h"

/* Compiles the raw (already unescaped) content of a quoted string or
 * heredoc token into an expression: a plain CL_EXPR_STRING when it has no
 * "${" or "%{" (outside of "$${"/"%%{" escapes), or a CL_EXPR_TEMPLATE
 * otherwise. base_line/base_col locate the start of `raw` in the source
 * file and are used to translate error positions found while parsing
 * embedded "${...}" / "%{...}" expressions back to real file
 * coordinates. */
cl_expr_t *cl_compile_template(cl_document_t *doc, const char *raw, int base_line, int base_col, cl_error_t *err);

#endif /* CL_TEMPLATE_H */
