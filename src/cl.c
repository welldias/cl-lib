#include "cl_internal.h"
#include "cl_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *cl_version(void) {
    static char version[16];
    snprintf(version, sizeof(version), "%d.%d.%d",
             CL_VERSION_MAJOR, CL_VERSION_MINOR, CL_VERSION_PATCH);
    return version;
}

static void cl_set_error(cl_error_t *err, int line, int col, const char *message) {
    if (!err) {
        return;
    }
    snprintf(err->message, sizeof(err->message), "%s", message);
    err->line = line;
    err->col = col;
}

cl_document_t *cl_load_string(const char *source, const char *source_name, cl_error_t *err) {
    cl_document_t *doc = calloc(1, sizeof(cl_document_t));
    if (!doc) {
        abort();
    }
    if (source_name) {
        doc->filename = cl_arena_strdup(doc, source_name);
    }

    if (cl_parser_parse(doc, source, err) != 0) {
        cl_document_free(doc);
        return NULL;
    }
    return doc;
}

cl_document_t *cl_load_file(const char *path, cl_error_t *err) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        cl_set_error(err, 0, 0, "could not open the file");
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        cl_set_error(err, 0, 0, "failed to read the file");
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        cl_set_error(err, 0, 0, "failed to read the file");
        return NULL;
    }

    char *buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(f);
        abort();
    }
    size_t read = fread(buffer, 1, (size_t)size, f);
    fclose(f);
    buffer[read] = '\0';

    cl_document_t *doc = cl_load_string(buffer, path, err);
    free(buffer);
    return doc;
}

void cl_document_free(cl_document_t *doc) {
    if (!doc) {
        return;
    }
    cl_arena_free_all(&doc->arena);
    free(doc);
}

cl_body_t *cl_document_root(cl_document_t *doc) {
    return doc->root;
}
