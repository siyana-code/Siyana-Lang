/* interp.h -- tree-walking evaluator for Siyana Lang.
 *
 * Phase 3 slice: Number and Character as the only primitive types, with
 * booleans derived from Number (decision 17). See
 * research/design/type-system.md.
 */

#ifndef SIYANA_INTERP_H
#define SIYANA_INTERP_H

#include <stdbool.h>

#include "ast.h"
#include "token.h"

typedef enum {
    VALUE_NIL,
    VALUE_NUMBER,
    VALUE_CHARACTER,
    VALUE_STRING
} ValueType;

typedef struct {
    ValueType type;
    union {
        long long number;
        unsigned char character;
        /* Borrowed from the AST node. Phase 3 never allocates strings at
         * runtime, so there is nothing to free here. */
        struct { const char *chars; size_t length; } string;
    } as;
    /* Set when the value came from the True or False literal, so output can show
     * the source form. Presentation only -- truthiness follows the number,
     * since bool is Number (decision 17).
     *
     * This is a separate flag rather than `value == 0 or 1` because False is
     * 0: testing the value would make the literal False indistinguishable from
     * an ordinary Number 0. */
    bool is_bool_literal;
} Value;

/* Names a declared type for diagnostics. Defined in interp.c because the
 * interpreter is where type errors are reported. */
const char *type_kind_name(TypeKind type);

typedef struct {
    bool had_error;
    char message[192];
    Position error_position;
} Interp;

/* Runs a program. Returns 0 on success, 1 on a runtime error with the
 * interpreter's error state populated. */
int interp_run(const Program *program, Interp *interp);

#endif /* SIYANA_INTERP_H */
