/* interp.h -- tree-walking evaluator for Siyana Lang.
 *
 * The integer types are fixed-width and overflow is an error rather than a
 * silent wrap. See research/design/integer-types.md.
 */

#ifndef SIYANA_INTERP_H
#define SIYANA_INTERP_H

#include <stdbool.h>

#include "ast.h"
#include "token.h"

/* An integer, stored in the widest C type and carrying the Siyana type that
 * governs its range. Arithmetic checks against that range and reports an error
 * rather than wrapping -- a size that wraps from 0xFFFFFFFF to 0 is a heap
 * overflow waiting to happen. */
typedef struct {
    long long bits;
    TypeKind type;
} Integer;

typedef enum {
    VALUE_NIL,
    VALUE_INTEGER,
    VALUE_STRING
} ValueType;

typedef struct {
    ValueType type;
    union {
        Integer integer;
        /* Borrowed from the AST node, except for Terminal.ReadLine which
         * returns an owned buffer documented at the call site. */
        struct { const char *chars; size_t length; } string;
    } as;
    /* Set when the value came from the True or False literal, so output can
     * show the source form. Presentation only -- truthiness follows the
     * integer, since bool is Number (decision 17).
     *
     * A separate flag rather than testing the value, because False is 0 and
     * would otherwise be indistinguishable from an ordinary Number 0. */
    bool is_bool_literal;
} Value;

typedef struct {
    bool had_error;
    char message[192];
    Position error_position;
} Interp;

/* Runs a program. Returns 0 on success, 1 on a runtime error with the
 * interpreter's error state populated. */
int interp_run(const Program *program, Interp *interp);

#endif /* SIYANA_INTERP_H */
