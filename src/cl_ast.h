#ifndef CL_AST_H
#define CL_AST_H

#include "cl_internal.h"

cl_body_t *cl_body_new(cl_document_t *doc);
cl_attribute_t *cl_body_append_attribute(cl_document_t *doc, cl_body_t *body, const char *name,
                                          cl_expr_t *value, int line, int col);
cl_block_t *cl_body_append_block(cl_document_t *doc, cl_body_t *body, const char *type,
                                  char **labels, size_t label_count, cl_body_t *block_body,
                                  int line, int col);

cl_expr_t *cl_expr_new_string(cl_document_t *doc, const char *value, int line, int col);
cl_expr_t *cl_expr_new_number(cl_document_t *doc, double value, int line, int col);
cl_expr_t *cl_expr_new_bool(cl_document_t *doc, int value, int line, int col);
cl_expr_t *cl_expr_new_null(cl_document_t *doc, int line, int col);
cl_expr_t *cl_expr_new_object(cl_document_t *doc, int line, int col);
cl_expr_t *cl_expr_new_traversal(cl_document_t *doc, const char *root, int line, int col);

void cl_expr_object_add(cl_document_t *doc, cl_expr_t *obj, const char *key, cl_expr_t *value);
void cl_expr_traversal_add_attr(cl_document_t *doc, cl_expr_t *trav, const char *name);
void cl_expr_traversal_add_index_number(cl_document_t *doc, cl_expr_t *trav, double index);
void cl_expr_traversal_add_index_string(cl_document_t *doc, cl_expr_t *trav, const char *name);
void cl_expr_traversal_add_splat_attr(cl_document_t *doc, cl_expr_t *trav);
void cl_expr_traversal_add_splat_full(cl_document_t *doc, cl_expr_t *trav);
void cl_expr_traversal_add_index_expr(cl_document_t *doc, cl_expr_t *trav, cl_expr_t *index);

cl_expr_t *cl_expr_new_postfix(cl_document_t *doc, cl_expr_t *base, int line, int col);
void cl_expr_postfix_add_attr(cl_document_t *doc, cl_expr_t *expr, const char *name);
void cl_expr_postfix_add_index_number(cl_document_t *doc, cl_expr_t *expr, double index);
void cl_expr_postfix_add_index_string(cl_document_t *doc, cl_expr_t *expr, const char *name);
void cl_expr_postfix_add_splat_attr(cl_document_t *doc, cl_expr_t *expr);
void cl_expr_postfix_add_splat_full(cl_document_t *doc, cl_expr_t *expr);
void cl_expr_postfix_add_index_expr(cl_document_t *doc, cl_expr_t *expr, cl_expr_t *index);

cl_expr_t *cl_expr_new_tuple(cl_document_t *doc, int line, int col);
void cl_expr_tuple_add(cl_document_t *doc, cl_expr_t *tuple, cl_expr_t *item);

cl_expr_t *cl_expr_new_template(cl_document_t *doc, cl_template_t *tpl, int line, int col);

cl_expr_t *cl_expr_new_call(cl_document_t *doc, const char *name, int expand_final, int line, int col);
void cl_expr_call_add_arg(cl_document_t *doc, cl_expr_t *call, cl_expr_t *arg);

cl_expr_t *cl_expr_new_unary(cl_document_t *doc, cl_unary_op_t op, cl_expr_t *operand, int line, int col);
cl_expr_t *cl_expr_new_binary(cl_document_t *doc, cl_binary_op_t op, cl_expr_t *left, cl_expr_t *right,
                               int line, int col);
cl_expr_t *cl_expr_new_conditional(cl_document_t *doc, cl_expr_t *cond, cl_expr_t *then_expr,
                                    cl_expr_t *else_expr, int line, int col);

cl_expr_t *cl_expr_new_for(cl_document_t *doc, const char *key_var, const char *val_var,
                            cl_expr_t *collection, cl_expr_t *key_expr, cl_expr_t *value_expr,
                            cl_expr_t *cond, int is_object, int grouping, int line, int col);

/* --- template mini-AST construction, used by cl_template.c --- */
cl_template_t *cl_template_new(cl_document_t *doc);
cl_template_part_t *cl_template_add_literal(cl_document_t *doc, cl_template_t *tpl, const char *text);
cl_template_part_t *cl_template_add_interp(cl_document_t *doc, cl_template_t *tpl, cl_expr_t *expr);
cl_template_part_t *cl_template_add_if(cl_document_t *doc, cl_template_t *tpl, cl_expr_t *cond,
                                        cl_template_t *then_tpl, cl_template_t *else_tpl);
cl_template_part_t *cl_template_add_for(cl_document_t *doc, cl_template_t *tpl, const char *key_var,
                                         const char *val_var, cl_expr_t *collection, cl_template_t *body);

/* Appends one label to a (still being parsed) label list, growing it as
 * needed. Used by the parser to accumulate a block's labels before the
 * block itself (and its body) exist. */
void cl_label_list_add(cl_document_t *doc, char ***labels, size_t *count, size_t *capacity, const char *label);

#endif /* CL_AST_H */
