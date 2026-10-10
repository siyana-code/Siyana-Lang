/* interp.h -- tree-walking evaluator for Siyana Lang.
 *
 * Phase 2 slice (decision 13): integers, bool, string literals, arithmetic,
 * comparison, if/else, while, functions, return, and variables.
 */

#ifndef SIYANA_INTERP_H
#define SIYANA_INTERP_H

#include <stdbool.h>

#include "ast.h"
#include "token.h"

typedef enum {
    VALUE_NIL,
    VALUE_BOOL,
    VALUE_INT,
    VALUE_STRING
} ValueType;

typedef struct {
    ValueType type;
    union {
        bool boolean;
        long long integer;
        /* Borrowed from the AST node. Phase 2 never allocates strings at
         * runtime, so there is nothing to free here. */
        struct { const char *chars; size_t length; } string;
    } as;
} Value;

typedef struct {
    bool had_error;
    char message[160];
    Position error_position;
} Interp;

/* Runs a program. Returns 0 on success, 1 on a runtime error with the
 * interpreter's error state populated. */
int interp_run(const Program *program, Interp *interp);

#endif /* SIYANA_INTERP_H */
