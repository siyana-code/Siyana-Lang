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
    VALUE_STRING,
    VALUE_STRUCT,
    VALUE_UNION,
    VALUE_RESULT,
    VALUE_ALLOCATOR
} ValueType;

/* A struct or union value.
 *
 * Both hold a reference to their declaration rather than copying the field
 * list, so a type can be registered after a value referencing it exists
 * without invalidating anything. Fields are a parallel array over the
 * declaration's fields; for a union only the slot at `tag` is meaningful. */
typedef struct Decl Decl;
typedef struct Value Value;

typedef struct {
    const Decl *decl;
    Value *fields;              /* owned */
    long long tag;              /* active variant for a union; -1 if none */
} Aggregate;

/* A fallible call's outcome: either the success value, or one variant of the
 * callee's declared error set.
 *
 * Not modelled as a Union, which was the first instinct and is wrong. A union
 * names one declaration; a Result holds a value of T *or* a variant of E, so it
 * spans two declarations and has to record which one the active side came from.
 * A dedicated kind also avoids synthesising an AST node at run time just to
 * stand in for a type the user never declared.
 *
 * `value` is owned by the Result and is NULL on the failure side. */
typedef struct {
    bool ok;
    Value *value;               /* owned, NULL when !ok */
    const struct Decl *error_decl;
    size_t error_variant;
} ResultValue;

struct Value {
    ValueType type;
    union {
        Integer integer;
        /* Borrowed from the AST node, except for Terminal.ReadLine which
         * returns an owned buffer documented at the call site. */
        struct { const char *chars; size_t length; } string;
        Aggregate aggregate;
        ResultValue result;
        /* An owned bump allocator. The arena is reference-counted here rather
         * than deep-copied, because copying an allocator should not copy its
         * memory: two copies handing out the same region would hand out the
         * same bytes twice. */
        struct Allocator *allocator;
    } as;
    /* Set when the value came from the True or False literal, so output can
     * show the source form. Presentation only -- truthiness follows the
     * integer, since bool is Number (decision 17).
     *
     * A separate flag rather than testing the value, because False is 0 and
     * would otherwise be indistinguishable from an ordinary Number 0.
     */
    bool is_bool_literal;
};


typedef struct {
    bool had_error;
    char message[192];
    Position error_position;
} Interp;

/* Runs a program. Returns 0 on success, 1 on a runtime error with the
 * interpreter's error state populated. */
int interp_run(const Program *program, Interp *interp);

#endif /* SIYANA_INTERP_H */
