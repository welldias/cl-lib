#ifndef CL_WRITER_H
#define CL_WRITER_H

#include "cl_internal.h"

#include <stddef.h>

/* Plain, non-arena growable buffer: its text is handed to the caller of the
 * cl_*_to_string() and cl_*_to_json() functions, who frees it. */
typedef struct cl_buf {
    char *data;
    size_t len;
    size_t capacity;
} cl_buf_t;

void cl_buf_append(cl_buf_t *buf, const char *s);
void cl_buf_append_char(cl_buf_t *buf, char c);
void cl_buf_append_indent(cl_buf_t *buf, int indent);
/* The buffer's text, or an empty malloc'd string when nothing was written. */
char *cl_buf_finish(cl_buf_t *buf);

/* Writes `s` between double quotes, escaped for the cl lexer. Every quoted
 * string is a template, so "${" and "%{" in literal text are written as
 * "$${" and "%%{" to read back as text. */
void cl_write_string_literal(cl_buf_t *buf, const char *s);
/* Same escaping, without the quotes (template literal parts). */
void cl_write_escaped_text(cl_buf_t *buf, const char *s);

/* Formats a finite number: integers without a fraction, anything else with
 * the fewest digits (15 to 17) that read back as exactly `value`. Returns
 * -1, leaving `out` empty, for NaN and infinities. */
int cl_format_number(char *out, size_t size, double value);

/* A number in cl syntax. Infinities are written as 1e999/-1e999, which read
 * back as infinities; NaN has no cl form and is written as null. */
void cl_write_number(cl_buf_t *buf, double value);

int cl_is_bare_ident(const char *s);

#endif /* CL_WRITER_H */
