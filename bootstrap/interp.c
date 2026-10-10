/* interp.c -- tree-walking evaluator for Siyana Lang.
 *
 * Structurally a direct walk of the AST. The exit states matter more than
 * speed here: every fallible operation returns a bool, and the error is
 * recorded once at the outermost layer rather than propagated as exceptions or
 * sentinels. There are no exceptions because the language will not have them.
 */

#include "interp.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Scopes ---------------------------------------------------------------- */

/* A lexical scope. Chains to its parent; functions close over their defining
 * scope so recursion and mutual reference work. */
typedef struct Scope {
    struct Scope *parent;
    struct {
        char *name;
        Value value;
    } *entries;
    size_t count;
    size_t capacity;
} Scope;

typedef struct {
    Stmt *declaration;   /* STMT_FUNCTION */
    Scope *closure;
} Function;
/* A failure travelling up the call stack.
 *
 * Carried on InterpState rather than on Flow because `Try` sits inside an
 * expression and evaluate() has no Flow to thread through. The alternative is
 * changing every evaluate() signature in the interpreter to plumb it, for a
 * signal that is almost always false.
 *
 * `propagating` is set by Try, noticed by the statement loop, and cleared by
 * the function-call boundary that finally owns it. It is a second exit path
 * alongside Flow.returned, and the two never both apply at once. */
typedef struct {
    bool propagating;
    Value error;                  /* a VALUE_RESULT carrying the failure */
    const Stmt *propagating_from; /* the function that set it */
} Propagation;


/* A registered Struct or Union declaration. Kept by pointer rather than copied
 * so a Value can reference it for its whole lifetime. */
struct Decl {
    const Stmt *node;          /* the STMT_STRUCT / STMT_UNION node */
    bool is_union;
};

typedef struct {
    const Program *program;
    Scope *global_scope;

    Function *functions;
    size_t function_count;
    size_t function_capacity;

    /* Struct and union declarations, registered before execution so a type may
     * be used before the line that declares it, as functions already are. */
    Decl *decls;
    size_t decl_count;
    size_t decl_capacity;

    /* The function whose body is running, so `Try` can check the callee's
     * error set against the caller's. NULL at the top level, where anything
     * that could fail is an error: there is no caller to report it to. */
    const Stmt *current_function;
    Propagation propagation;

    bool failed;
    char message[192];
    Position error_position;
} InterpState;

static Scope *scope_create(Scope *parent) {
    Scope *scope = calloc(1, sizeof(Scope));
    if (scope == NULL) return NULL;
    scope->parent = parent;
    return scope;
}

/* Frees exactly one scope and its entries, leaving `parent` untouched. */
static void scope_destroy(Scope *scope) {
    if (scope == NULL) return;
    for (size_t i = 0; i < scope->count; i++) free(scope->entries[i].name);
    free(scope->entries);
    free(scope);
}

/* Frees a scope together with everything it encloses.
 *
 * A call scope's parent is its closure, which in turn chains to the global
 * scope. Freeing that chain when the call returns would take the interpreter's
 * own scopes with it, so this is only for teardown at the very end of a run --
 * never for leaving a nested scope. Leaving a nested scope uses scope_destroy,
 * which frees exactly one level. */
static void scope_destroy_chain(Scope *scope) {
    while (scope != NULL) {
        Scope *parent = scope->parent;
        scope_destroy(scope);
        scope = parent;
    }
}

static bool scope_define(Scope *scope, const char *name, Value value) {
    if (scope->count + 1 > scope->capacity) {
        size_t capacity = scope->capacity ? scope->capacity * 2 : 8;
        void *grown = realloc(scope->entries, capacity * sizeof(*scope->entries));
        if (grown == NULL) return false;
        scope->entries = grown;
        scope->capacity = capacity;
    }
    char *owned = malloc(strlen(name) + 1);
    if (owned == NULL) return false;
    strcpy(owned, name);
    scope->entries[scope->count].name = owned;
    scope->entries[scope->count].value = value;
    scope->count++;
    return true;
}

static bool scope_contains(const Scope *scope, const char *name) {
    for (size_t i = 0; i < scope->count; i++) {
        if (strcmp(scope->entries[i].name, name) == 0) return true;
    }
    return false;
}

static bool scope_get(const Scope *scope, const char *name, Value *out) {
    for (; scope != NULL; scope = scope->parent) {
        for (size_t i = 0; i < scope->count; i++) {
            if (strcmp(scope->entries[i].name, name) == 0) {
                *out = scope->entries[i].value;
                return true;
            }
        }
    }
    return false;
}

/* Returns a pointer to a variable's storage, or NULL.
 *
 * Distinct from scope_get, which returns a copy of the value. Field assignment
 * needs the storage so a write is visible to every later read. */
static Value *scope_find(Scope *scope, const char *name) {
    for (; scope != NULL; scope = scope->parent) {
        for (size_t i = 0; i < scope->count; i++) {
            if (strcmp(scope->entries[i].name, name) == 0) {
                return &scope->entries[i].value;
            }
        }
    }
    return NULL;
}

static bool scope_assign(Scope *scope, const char *name, Value value) {
    for (; scope != NULL; scope = scope->parent) {
        for (size_t i = 0; i < scope->count; i++) {
            if (strcmp(scope->entries[i].name, name) == 0) {
                scope->entries[i].value = value;
                return true;
            }
        }
    }
    return false;
}

/* --- Errors ---------------------------------------------------------------- */

static void fail(InterpState *state, Position position, const char *fmt, ...) {
    if (state->failed) return;
    state->failed = true;
    state->error_position = position;
    va_list args;
    va_start(args, fmt);
    vsnprintf(state->message, sizeof(state->message), fmt, args);
    va_end(args);
}

/* --- Integer types --------------------------------------------------------- */

const char *type_kind_name(TypeKind type) {
    switch (type) {
        case TYPE_NUMBER:    return "Number";
        case TYPE_NUMBER32:  return "Number32";
        case TYPE_WORD:      return "Word";
        case TYPE_CHARACTER: return "Character";
        case TYPE_NAMED:     return "a named type";
        default:             return "unknown";
    }
    }

/* Size in bytes. `Word` reports the pointer width of the build target, which
 * is the one width that is not fixed. */
size_t type_kind_size(TypeKind type) {
    switch (type) {
        case TYPE_NUMBER:    return 8;
        case TYPE_NUMBER32:  return 4;
        case TYPE_CHARACTER: return 1;
        case TYPE_WORD:      return sizeof(void *);
        default:             return 0;
    }
}

bool type_kind_is_unsigned(TypeKind type) {
    return type == TYPE_WORD || type == TYPE_CHARACTER;
}

/* Representable range for each type. Computed rather than written out so the
 * two's-complement assumption is stated once. */
typedef struct {
    long long min;
    unsigned long long max;
} Range;

static Range range_of(TypeKind type) {
    Range range;
    switch (type) {
        case TYPE_NUMBER:
            /* int64 in two's complement. LONG_MIN is avoided so the literal
             * does not depend on the host's long width. */
            range.min = -9223372036854775807LL - 1;
            range.max = 9223372036854775807ULL;
            break;
        case TYPE_NUMBER32:
            range.min = -2147483647LL - 1;
            range.max = 2147483647ULL;
            break;
        case TYPE_WORD:
            /* Word is unsigned and pointer-width. */
            range.min = 0;
            range.max = (sizeof(void *) >= 8) ? 18446744073709551615ULL
                                              : 4294967295ULL;
            break;
        case TYPE_CHARACTER:
            range.min = 0;
            range.max = 255;
            break;
        default:
            range.min = 0;
            range.max = 0;
            break;
    }
    return range;
}

/* True when `value` fits `type`. Used both to validate literals and to check
 * arithmetic results. */
static bool integer_fits(long long value, TypeKind type) {
    Range range = range_of(type);
    if (value < 0) {
        /* A negative value only fits a signed type, and only down to its min. */
        if (type_kind_is_unsigned(type)) return false;
        return value >= range.min;
    }
    return (unsigned long long)value <= range.max;
}

/* True when a value's runtime type matches its declared type. Integer values
 * carry their own Siyana type, so this is a comparison rather than a category
 * check -- which is what makes `Variable x: Number32 = 5` legal while
 * `Variable x: Number32 = "a"` is not. */
static bool value_matches_type(Value value, TypeKind declared) {
    if (value.type != VALUE_INTEGER) return false;
    return value.as.integer.type == declared;
}

static const char *type_name_of(Value value) {
    if (value.type == VALUE_INTEGER) return type_kind_name(value.as.integer.type);
    switch (value.type) {
        case VALUE_NIL:    return "nothing";
        case VALUE_STRING: return "String";
        default:           return "unknown";
    }
}

/* C-style truthiness, following decision 17: zero is False, anything else is
 * True.
 *
 * This is the deliberate reduction in safety that decision 17 records. The
 * Phase 2 interpreter rejected `If (1)`; this accepts it, which is what C
 * does and what keeps the type system to four integer types. */
static bool is_truthy(Value value) {
    return value.type == VALUE_INTEGER && value.as.integer.bits != 0;
}

/* --- Control flow ---------------------------------------------------------- */

/* `return` unwinds by setting this flag and returning the value. The C code
 * checks it after each statement. An OS language cannot afford exceptions, and
 * this is the shape Phase 3's error unions will formalise. */
typedef struct {
    bool returned;
    Value return_value;
} Flow;


static Value value_nil(void) {
    Value value = {0};
    value.type = VALUE_NIL;
    return value;
}

/* --- Forward declarations ------------------------------------------------- */

/* Defined with the aggregate helpers, but needed earlier for named-type
 * resolution in variable and parameter checks. */
/* Copies an aggregate value, allocating a fresh fields array.
 *
 * Structs are value types, so `b = a` must not leave `b` sharing `a`'s fields.
 * Without this copy, assigning through one variable silently changes the other,
 * which is the kind of aliasing bug that is very hard to find later. */
static Value copy_aggregate(Value value) {
    Value copy = value;
    size_t count = value.as.aggregate.decl->node->field_count;
    copy.as.aggregate.fields = malloc(count * sizeof(Value));
    if (copy.as.aggregate.fields != NULL) {
        for (size_t i = 0; i < count; i++) {
            copy.as.aggregate.fields[i] = value.as.aggregate.fields[i];
        }
    }
    return copy;
}

/* Structs and unions are value types: reading one out of a variable or
 * passing one to a function copies it. */
static Value value_copy(Value value) {
    if (value.type == VALUE_STRUCT || value.type == VALUE_UNION) {
        return copy_aggregate(value);
    }
    return value;
}

static const Decl *resolve_named(const InterpState *state, const char *name);

static bool execute_statement(InterpState *state, const Stmt *statement,
                              Scope *scope, Flow *flow);
static bool evaluate(InterpState *state, const Expr *expression,
                     Scope *scope, Value *out);

/* Defined with the error helpers below, but needed by call_function, which
 * wraps every declared Result on the way out. */
static bool make_result_ok(Value inner, Value *out);

/* Used by the Catch arms; declared here because a Catch can appear inside any
 * expression, including one evaluated long before this function is defined. */
static bool execute_block(InterpState *state, Stmt *const *body, size_t count,
                          Scope *scope, Flow *flow);

/* --- Values ---------------------------------------------------------------- */

/* Builds an integer value of the given Siyana type. Callers are responsible
 * for having already checked that `bits` fits `type`; arithmetic below does
 * that check before calling. */
static Value value_integer(long long bits, TypeKind type) {
    Value value = {0};
    value.type = VALUE_INTEGER;
    value.as.integer.bits = bits;
    value.as.integer.type = type;
    return value;
}

static Value value_number(long long bits) {
    return value_integer(bits, TYPE_NUMBER);
}

static Value value_string(const char *chars, size_t length) {
    Value value = {0};
    value.type = VALUE_STRING;
    value.as.string.chars = chars;
    value.as.string.length = length;
    return value;
}

/* String equality compares contents, not pointers. Pointer comparison would
 * make "a" == "a" depend on how the lexer allocated, which is exactly the
 * hidden behaviour the language is meant to avoid. */
static bool strings_equal(Value a, Value b) {
    if (a.as.string.length != b.as.string.length) return false;
    return memcmp(a.as.string.chars, b.as.string.chars, a.as.string.length) == 0;
}

/* --- Binary operators ------------------------------------------------------ */


static const char *operator_symbol(Operator op) {
    switch (op) {
        case OP_ADD:           return "+";
        case OP_SUBTRACT:      return "-";
        case OP_MULTIPLY:      return "*";
        case OP_DIVIDE:        return "/";
        case OP_EQUAL:         return "==";
        case OP_NOT_EQUAL:     return "!=";
        case OP_LESS:          return "<";
        case OP_LESS_EQUAL:    return "<=";
        case OP_GREATER:       return ">";
        case OP_GREATER_EQUAL: return ">=";
        default:               return "?";
    }
}

/* A comparison result. Comparisons yield True or False, not 1 and 0, because
 * they are conceptually booleans even though bool is Number (decision 17) --
 * printing `3 == 3` as `1` would read as a bug to anyone expecting a
 * comparison. Truthiness still follows the number.
 *
 * The source form is a property of the expression that produced the value, so
 * it does not travel with the value. `Terminal.Write(a == b)` prints True;
 * once `a == b` is assigned to a Number and that variable is printed, it is a
 * Number holding 1 and prints 1. Passing `True` into a function likewise
 * prints 1 inside, because the callee received a Number, not a literal. */
static Value value_bool_result(bool truth) {
    Value value = value_number(truth ? 1 : 0);
    value.is_bool_literal = true;
    return value;
}

/* Strips the source-form flag. Applied when a value crosses a binding or an
 * argument boundary, where only the underlying Number survives. */
static Value value_as_plain(Value value) {
    value.is_bool_literal = false;
    return value;
}

/* Arithmetic on fixed-width integers.
 *
 * Two rules make this safe, and both are from
 * research/design/integer-types.md:
 *
 *   1. Both operands must be the same type. Mixing widths silently is the
 *      origin of a large class of integer bugs.
 *   2. The result must fit that type. Overflow is an error, never a wrap --
 *      a size that wraps from 0xFFFFFFFF to 0 is a heap overflow.
 */

/* Computes `a op b` and reports whether it fits `type`.
 *
 * The operation is performed on unsigned values so that the overflow check is
 * not defeated by signed overflow in the host language, which is undefined
 * behaviour in C.
 *
 * For unsigned types a plain comparison of the result against the width mask
 * is not enough: `Word(0) - 1` wraps to exactly 0xFFFFFFFFFFFFFFFF, which
 * equals the mask and would pass. Subtraction therefore also carries a borrow,
 * which is the real signal that the result is negative.
 */
static bool checked_arithmetic(Operator op, long long a, long long b,
                               TypeKind type, long long *out_result,
                               const char **out_reason) {
    unsigned long long ua = (unsigned long long)a;
    unsigned long long ub = (unsigned long long)b;
    unsigned long long ur = 0;
    bool division_by_zero = false;
    bool borrow = false;

    switch (op) {
        case OP_ADD:      ur = ua + ub; break;
        case OP_SUBTRACT:
            ur = ua - ub;
            borrow = (ub > ua);   /* the true signal of an underflow */
            break;
        case OP_MULTIPLY: ur = ua * ub; break;
        case OP_DIVIDE:
            if (b == 0) {
                division_by_zero = true;
                break;
            }
            ur = ua / ub;
            break;
        default:
            *out_reason = "unhandled operator";
            return false;
    }

    if (division_by_zero) {
        *out_reason = "division by zero";
        return false;
    }

    /* An unsigned type can never produce a negative result, so a borrow is an
     * error regardless of how the wrapped bits happen to look. */
    if (borrow && type_kind_is_unsigned(type)) {
        *out_reason = "overflow";
        return false;
    }

    /* Reinterpret in the target type's width and check the result fits. A
     * result that needed bits above the width will not fit and is reported. */
    unsigned long long mask;
    switch (type) {
        case TYPE_CHARACTER: mask = 0xFFULL; break;
        case TYPE_NUMBER32:   mask = 0xFFFFFFFFULL; break;
        case TYPE_WORD:       mask = (sizeof(void *) >= 8) ? ~0ULL : 0xFFFFFFFFULL; break;
        case TYPE_NUMBER:     mask = ~0ULL; break;
        default:              mask = 0; break;
    }

    unsigned long long narrowed = ur & mask;
    long long signed_result = (long long)narrowed;

    /* For signed types, the wrapped bit pattern must have the sign bit clear
     * or set consistently -- otherwise it came from an overflow. */
    if (type == TYPE_NUMBER32 || type == TYPE_NUMBER) {
        unsigned long long limit = (type == TYPE_NUMBER32) ? 0x7FFFFFFFULL : 0x7FFFFFFFFFFFFFFFULL;
        if (ur > limit) {
            *out_reason = "overflow";
            return false;
        }
        /* A negative result arises when the unsigned computation underflowed;
         * that is valid for subtraction but must not be produced by, say, an
         * unsigned type at all. */
        if (narrowed != ur) {
            *out_reason = "overflow";
            return false;
        }
        *out_result = signed_result;
        return true;
    }

    /* Unsigned types: any value above the mask overflowed. */
    if (ur > mask) {
        *out_reason = "overflow";
        return false;
    }
    *out_result = (long long)ur;
    return true;
}

static int compare_integers(long long a, long long b) {
    return (a > b) - (a < b);
}

/* True when `expression` is a bare integer literal that has not yet been given
 * a type by its context.
 *
 * `is_untyped_literal` must agree with adopt_operand_type's precondition. They
 * are checked together here so the two cannot drift. */
static bool is_untyped_literal(const Expr *expression, Value value) {
    return expression != NULL && expression->type == EXPR_NUMBER &&
           value.type == VALUE_INTEGER && value.as.integer.type == TYPE_NUMBER;
}

/* Gives an untyped integer literal the type of the operand it is paired with.
 *
 * Without this, `w - 30` would be a type error because `30` evaluated as a
 * Number while `w` is a Word -- which would make every arithmetic expression
 * need a cast, and `Word` effectively unusable.
 *
 * Adoption is allowed toward any integer type, provided the value fits. The
 * signed/unsigned direction is included deliberately: `w - 1` and `c + 1` are
 * the most ordinary expressions in the language, and refusing them would be
 * crippling in a way no real systems language does. The protection against
 * silently turning a negative literal into an unsigned value is *not* lost --
 * `integer_fits` rejects -1 for Word, so the case that actually matters is
 * still an error. A positive literal is unambiguously that literal's value
 * whichever type it becomes. */
static bool adopt_operand_type(const Expr *expression, Value *value,
                               Value from_other_side) {
    if (!is_untyped_literal(expression, *value)) return false;
    if (from_other_side.type != VALUE_INTEGER) return false;

    TypeKind wanted = from_other_side.as.integer.type;
    if (wanted == TYPE_NUMBER) return false;
    if (!integer_fits((*value).as.integer.bits, wanted)) return false;

    (*value).as.integer.type = wanted;
    return true;
}

static bool evaluate_binary(InterpState *state, const Expr *expression,
                            Scope *scope, Value *out) {
    Value left;
    Value right;
    if (!evaluate(state, expression->left, scope, &left)) return false;
    if (!evaluate(state, expression->right, scope, &right)) return false;

    /* Contextual typing, applied before the operator is dispatched.
     *
     * Skipped for equality: comparing a Number against a Character literal
     * should report a type error rather than silently producing False, and
     * letting `1` adopt Character's type would turn a real mistake into a
     * plausible-looking answer. */
    if (expression->op != OP_EQUAL && expression->op != OP_NOT_EQUAL) {
        adopt_operand_type(expression->left, &left, right);
        adopt_operand_type(expression->right, &right, left);
    }

    /* Equality between strings compares content; between integers it needs
     * matching types. Anything else is a type error rather than silently
     * false, because a mismatch here is almost always a mistake. */
    if (expression->op == OP_EQUAL || expression->op == OP_NOT_EQUAL) {
        if (left.type == VALUE_STRING || right.type == VALUE_STRING) {
            if (left.type != right.type) {
                fail(state, expression->position,
                     "cannot compare %s with %s using '%s'",
                     type_name_of(left), type_name_of(right),
                     operator_symbol(expression->op));
                return false;
            }
            *out = value_bool_result(strings_equal(left, right) ==
                                     (expression->op == OP_EQUAL));
            return true;
        }

        if (left.type != right.type) {
            fail(state, expression->position,
                 "cannot compare %s with %s using '%s'",
                 type_name_of(left), type_name_of(right),
                 operator_symbol(expression->op));
            return false;
        }

        if (left.type == VALUE_NIL) {
            *out = value_bool_result(expression->op == OP_EQUAL);
            return true;
        }

        /* Same type required. Number32 == Number is an error, not False: the
         * two mean different things and a mismatch is worth reporting. */
        if (left.as.integer.type != right.as.integer.type) {
            fail(state, expression->position,
                 "cannot compare %s with %s using '%s'",
                 type_kind_name(left.as.integer.type),
                 type_kind_name(right.as.integer.type),
                 operator_symbol(expression->op));
            return false;
        }

        bool equal = left.as.integer.bits == right.as.integer.bits;
        *out = value_bool_result(equal == (expression->op == OP_EQUAL));
        return true;
    }

    /* Every other operator needs two integers of the same type. */
    if (left.type != VALUE_INTEGER || right.type != VALUE_INTEGER) {
        fail(state, expression->position,
             "cannot apply '%s' to %s and %s",
             operator_symbol(expression->op), type_name_of(left), type_name_of(right));
        return false;
    }

    TypeKind type = left.as.integer.type;
    if (right.as.integer.type != type) {
        fail(state, expression->position,
             "cannot apply '%s' to %s and %s -- both operands must be the same type",
             operator_symbol(expression->op), type_kind_name(type),
             type_kind_name(right.as.integer.type));
        return false;
    }

    long long a = left.as.integer.bits;
    long long b = right.as.integer.bits;

    /* Unsigned types compare as unsigned, or a negative left operand would
     * look smaller than any positive right operand. */
    int comparison;
    if (type_kind_is_unsigned(type)) {
        unsigned long long ua = (unsigned long long)a;
        unsigned long long ub = (unsigned long long)b;
        comparison = (ua > ub) - (ua < ub);
    } else {
        comparison = compare_integers(a, b);
    }

    switch (expression->op) {
        case OP_LESS:          *out = value_bool_result(comparison <  0); return true;
        case OP_LESS_EQUAL:    *out = value_bool_result(comparison <= 0); return true;
        case OP_GREATER:       *out = value_bool_result(comparison >  0); return true;
        case OP_GREATER_EQUAL: *out = value_bool_result(comparison >= 0); return true;
        default: break;
    }

    long long result = 0;
    const char *reason = NULL;
    if (!checked_arithmetic(expression->op, a, b, type, &result, &reason)) {
        if (reason != NULL && strcmp(reason, "unhandled operator") == 0) {
            fail(state, expression->position, "unhandled operator '%s'",
                 operator_symbol(expression->op));
        } else {
            fail(state, expression->position, "%s in %s arithmetic", reason,
                 type_kind_name(type));
        }
        return false;
    }

    *out = value_integer(result, type);
    return true;
}


/* Negation is checked against the type's minimum rather than through
 * checked_arithmetic: that path computes in unsigned space, so `0 - 7` would
 * look like an underflow rather than the ordinary signed negation `-7`. The
 * minimum is also the one value with no positive counterpart. */
static bool checked_negate(long long bits, TypeKind type, long long *out_result) {
    if (type_kind_is_unsigned(type)) return false;
    if (bits == range_of(type).min) return false;
    *out_result = -bits;
    return true;
}
static bool evaluate_unary(InterpState *state, const Expr *expression,
                            Scope *scope, Value *out) {
    Value operand;
    if (!evaluate(state, expression->right, scope, &operand)) return false;

    if (expression->op == OP_NOT) {
        /* C semantics: !0 is 1, anything else is 0. */
        *out = value_bool_result(!is_truthy(operand));
        return true;
    }

    if (operand.type != VALUE_INTEGER) {
        fail(state, expression->position,
             "operator '-' requires an integer operand, got %s",
             type_name_of(operand));
        return false;
    }

    TypeKind type = operand.as.integer.type;
    long long bits = operand.as.integer.bits;

    /* Negation is checked against the type's minimum rather than through
     * checked_arithmetic: that path computes in unsigned space, so `0 - 7`
     * would look like an underflow rather than the ordinary signed negation
     * `-7`. The minimum is also the one value with no positive counterpart. */
    long long negated;
    if (!checked_negate(bits, type, &negated)) {
        if (type_kind_is_unsigned(type)) {
            fail(state, expression->position,
                 "cannot negate %s, which has no negative values",
                 type_kind_name(type));
        } else {
            fail(state, expression->position,
                 "cannot negate %s: %lld is the smallest value it can hold",
                 type_kind_name(type), bits);
        }
        return false;
    }

    *out = value_integer(negated, type);
    return true;
}
/* --- Functions ------------------------------------------------------------- */

static bool register_function(InterpState *state, const Stmt *declaration, Scope *closure) {
    if (state->function_count + 1 > state->function_capacity) {
        size_t capacity = state->function_capacity ? state->function_capacity * 2 : 8;
        Function *grown = realloc(state->functions, capacity * sizeof(Function));
        if (grown == NULL) {
            fail(state, declaration->position, "out of memory");
            return false;
        }
        state->functions = grown;
        state->function_capacity = capacity;
    }
    state->functions[state->function_count].declaration = (Stmt *)declaration;
    state->functions[state->function_count].closure = closure;
    state->function_count++;
    return true;
}

static const Function *find_function(const InterpState *state, const char *name) {
    /* Searched in reverse so a later declaration shadows an earlier one, which
     * is what allows mutual recursion to be written in any order. */
    for (size_t i = state->function_count; i > 0; i--) {
        const Stmt *declaration = state->functions[i - 1].declaration;
        if (strcmp(declaration->name, name) == 0) return &state->functions[i - 1];
    }
    return NULL;
}

static bool call_function(InterpState *state, const Function *function,
                          Value *arguments, size_t argument_count, Value *out) {
    const Stmt *declaration = function->declaration;

    if (argument_count != declaration->parameter_count) {
        fail(state, declaration->position,
             "function '%s' expects %zu argument%s, got %zu",
             declaration->name, declaration->parameter_count,
             declaration->parameter_count == 1 ? "" : "s", argument_count);
        return false;
    }

    Scope *call_scope = scope_create(function->closure);
    if (call_scope == NULL) {
        fail(state, declaration->position, "out of memory");
        return false;
    }

    for (size_t i = 0; i < argument_count; i++) {
        const Parameter *parameter = &declaration->parameters[i];
        /* Type agreement is checked here rather than at parse time. Phase 3
         * replaces this with real static checking; for now it catches the
         * common mistake and gives a message that names the parameter. */
        /* The literal form is dropped on the way in: the callee received a Number,
         * not the expression `True`. */
        /* Same literal-narrowing rule as variable initialisers, and for the
         * same reason: a Number literal may become a Number32 or Character
         * when it fits, but nothing is inferred across the signed/unsigned
         * boundary. */
        Value argument = value_as_plain(arguments[i]);
        TypeKind expected = parameter->type;
        if (expected == TYPE_NAMED) {
            /* Same identity check as a variable initialiser: a value of a
             * different struct type is rejected even if its fields match. */
            const Decl *decl = resolve_named(state, parameter->type_name);
            if (decl == NULL) {
                fail(state, parameter->position,
                     "undefined type '%s'", parameter->type_name);
                scope_destroy(call_scope);
                return false;
            }
            if (argument.type != (decl->is_union ? VALUE_UNION : VALUE_STRUCT) ||
                argument.as.aggregate.decl != decl) {
                fail(state, parameter->position,
                     "parameter '%s' of '%s' expects %s, got %s",
                     parameter->name, declaration->name,
                     parameter->type_name, type_name_of(argument));
                scope_destroy(call_scope);
                return false;
            }
        } else if (!value_matches_type(argument, expected)) {
            TypeKind given = argument.type == VALUE_INTEGER
                                 ? argument.as.integer.type
                                 : TYPE_NUMBER;
            if (argument.type == VALUE_INTEGER && given == TYPE_NUMBER &&
                integer_fits(argument.as.integer.bits, expected)) {
                argument = value_integer(argument.as.integer.bits, expected);
            } else {
                fail(state, parameter->position,
                     "parameter '%s' of '%s' expects %s, got %s",
                     parameter->name, declaration->name,
                     type_kind_name(expected), type_name_of(argument));
                scope_destroy(call_scope);
                return false;
            }
        }
        if (!scope_define(call_scope, parameter->name, value_copy(argument))) {
            fail(state, parameter->position, "out of memory");
            scope_destroy(call_scope);
            return false;
        }
    }

    const Stmt *enclosing = state->current_function;
    state->current_function = declaration;
    state->propagation.propagating = false;
    state->propagation.propagating_from = NULL;

    Flow flow = {0};
    for (size_t i = 0; i < declaration->body_count; i++) {
        if (!execute_statement(state, declaration->body[i], call_scope, &flow)) {
            state->current_function = enclosing;
            scope_destroy(call_scope);
            return false;
        }
        /* A propagating error is this function's return value, so the body
         * stops here exactly as it would for a Return. */
        if (flow.returned || state->propagation.propagating) break;
    }

    if (declaration->declared_type == TYPE_RESULT) {
        if (state->propagation.propagating) {
            /* Hand the failure to whoever called this function. Rule 1 already
             * guarantees they must have written a Try or a Catch, so there is
             * nothing to check here -- including when *they* are the top level,
             * where `F(2) Catch { ... }` is a perfectly good program. */
            *out = state->propagation.error;
            state->propagation.propagating = false;
        } else {
            if (!make_result_ok(flow.returned ? flow.return_value : value_nil(), out)) {
                fail(state, declaration->position, "out of memory");
                state->current_function = enclosing;
                scope_destroy(call_scope);
                return false;
            }
        }
    } else if (state->propagation.propagating) {
        /* Rule 4: a function that declares no error set cannot raise one. */
        fail(state, declaration->position,
             "'%s' propagates an error but does not declare an error set",
             declaration->name);
        state->current_function = enclosing;
        scope_destroy(call_scope);
        return false;
    } else {
        *out = flow.returned ? flow.return_value : value_nil();
    }

    state->current_function = enclosing;
    scope_destroy(call_scope);
    return true;
}

/* --- Expressions ----------------------------------------------------------- */

/* Prints a value using the representation a programmer expects to see.
 * Characters print as the character, not as a number -- printing 65 rather
 * than 'A' would make Character painful to debug. */
static void write_value(Value value) {
    switch (value.type) {
        case VALUE_INTEGER:
            if (value.is_bool_literal) {
                /* True and False print in their source form. The distinction
                 * only exists for the literal, so `Variable x: Number = True`
                 * prints 1 -- honest, since it is a Number holding 1. */
                printf("%s", value.as.integer.bits != 0 ? "True" : "False");
            } else if (value.as.integer.type == TYPE_CHARACTER) {
                /* A Character prints as the character it denotes. Printing 65
                 * for 'A' would make Character painful to debug. */
                putchar((int)value.as.integer.bits);
            } else {
                printf("%lld", value.as.integer.bits);
            }
            break;
        case VALUE_STRING:
            fwrite(value.as.string.chars, 1, value.as.string.length, stdout);
            break;
        case VALUE_STRUCT: {
            /* Printed as `Name { a: 1, b: 2 }` so a struct's contents are
             * visible rather than an opaque handle. */
            const Decl *decl = value.as.aggregate.decl;
            printf("%s {", decl->node->name);
            for (size_t i = 0; i < decl->node->field_count; i++) {
                if (i > 0) printf(", ");
                printf("%s: ", decl->node->fields[i].name);
                write_value(value.as.aggregate.fields[i]);
            }
            printf("}");
            break;
        }
        case VALUE_UNION: {
            /* Only the active variant is printed: the others hold nothing. */
            const Decl *decl = value.as.aggregate.decl;
            if (value.as.aggregate.tag < 0) {
                printf("%s {uninitialised}", decl->node->name);
                break;
            }
            const Field *variant = &decl->node->fields[value.as.aggregate.tag];
            printf("%s.%s", decl->node->name, variant->name);
            if (variant->has_payload) {
                printf("(");
                write_value(value.as.aggregate.fields[value.as.aggregate.tag]);
                printf(")");
            }
            break;
        }
        default:
            printf("nothing");
            break;
        case VALUE_RESULT:
            /* Only reachable if a Result is printed directly, which rule 1 is
             * meant to prevent. Shown rather than hidden so that if the rule is
             * ever relaxed, the output says what happened. */
            if (value.as.result.ok) {
                printf("Ok(");
                if (value.as.result.value != NULL) write_value(*value.as.result.value);
                printf(")");
            } else if (value.as.result.error_decl != NULL &&
                       value.as.result.error_variant <
                           value.as.result.error_decl->node->field_count) {
                printf("Error.%s",
                       value.as.result.error_decl->node->fields[value.as.result.error_variant].name);
            } else {
                printf("Error");
            }
            break;
    }
}

static bool evaluate_builtin(InterpState *state, const Expr *expression,
                             Scope *scope, Value *out) {
    if (expression->builtin == TOKEN_WRITE) {
        /* Variadic: writes each argument in turn, then a newline. A single
         * argument is the common case, but `Terminal.Write("x: ", n)` reads
         * better than two calls and costs nothing. */
        if (expression->argument_count == 0) {
            fail(state, expression->position,
                 "Terminal.Write needs at least 1 argument");
            return false;
        }
        for (size_t i = 0; i < expression->argument_count; i++) {
            Value argument;
            if (!evaluate(state, expression->arguments[i], scope, &argument)) return false;
            write_value(argument);
        }

        /* Terminal.Write appends a newline, matching a shell that writes one
         * line at a time. A future WriteRaw writes without one; that is a
         * library addition, not a language one. */
        putchar('\n');
        *out = value_nil();
        return true;
    }

    if (expression->builtin == TOKEN_READ_LINE) {
        if (expression->argument_count != 0) {
            fail(state, expression->position,
                 "Terminal.ReadLine expects 0 arguments, got %zu", expression->argument_count);
            return false;
        }

        /* Reads one line into a heap buffer owned by the caller. Phase 3 does
         * not yet expose the allocator model, so this is the one place the
         * interpreter allocates a value the language cannot free; it is noted
         * rather than hidden. */
        size_t capacity = 128;
        size_t length = 0;
        char *buffer = malloc(capacity);
        if (buffer == NULL) {
            fail(state, expression->position, "out of memory reading input");
            return false;
        }

        int c;
        bool any = false;
        while ((c = getchar()) != EOF) {
            any = true;
            if (c == '\n') break;
            if (length + 1 >= capacity) {
                capacity *= 2;
                char *grown = realloc(buffer, capacity);
                if (grown == NULL) {
                    free(buffer);
                    fail(state, expression->position, "out of memory reading input");
                    return false;
                }
                buffer = grown;
            }
            buffer[length++] = (char)c;
        }
        buffer[length] = '\0';

        if (!any && length == 0) {
            free(buffer);
            *out = value_nil();
            return true;
        }

        /* Ownership passes to the returned value. A subsequent ReadLine
         * invalidates it, which is documented rather than defended against:
         * proper ownership needs the allocator model from decision 14. */
        *out = value_string(buffer, length);
        return true;
    }

    fail(state, expression->position, "unknown builtin");
    return false;
}

static bool evaluate_call(InterpState *state, const Expr *expression,
                          Scope *scope, Value *out) {
    if (expression->type == EXPR_BUILTIN) {
        return evaluate_builtin(state, expression, scope, out);
    }

    if (expression->callee->type != EXPR_IDENT) {
        fail(state, expression->position,
             "can only call named functions in Phase 3");
        return false;
    }

    const char *name = expression->callee->name;

    const Function *function = find_function(state, name);
    if (function == NULL) {
        fail(state, expression->position, "undefined function '%s'", name);
        return false;
    }

    Value *arguments = NULL;
    if (expression->argument_count > 0) {
        arguments = calloc(expression->argument_count, sizeof(Value));
        if (arguments == NULL) {
            fail(state, expression->position, "out of memory");
            return false;
        }
    }

    bool ok = true;
    for (size_t i = 0; i < expression->argument_count; i++) {
        if (!evaluate(state, expression->arguments[i], scope, &arguments[i])) {
            ok = false;
            break;
        }
    }

    if (ok) ok = call_function(state, function, arguments, expression->argument_count, out);
    free(arguments);
    if (!ok) return false;

    /* Rule 1: a call that can fail must be handled at the point of the call.
     * The parser marked this node when it is the operand of Try or Catch, which
     * is the only place that relationship is visible. */
    if (function->declaration->declared_type == TYPE_RESULT && !expression->handled) {
        fail(state, expression->position,
             "'%s' can fail; follow the call with Try or Catch", name);
        return false;
    }

    return true;
}

/* --- Aggregates: structs and unions ---------------------------------------- */

/* Finds a registered Struct or Union by name, or NULL. */
static const Decl *find_decl(const InterpState *state, const char *name) {
    for (size_t i = 0; i < state->decl_count; i++) {
        if (strcmp(state->decls[i].node->name, name) == 0) return &state->decls[i];
    }
    return NULL;
}

/* Resolves a declared type name to the aggregate it refers to. Returns NULL
 * when the name is not a known Struct or Union, which is how an undeclared
 * type is reported. */
static const Decl *resolve_named(const InterpState *state, const char *name) {
    if (name == NULL) return NULL;
    return find_decl(state, name);
}

/* Declares and aggregates share the same field table; this finds a field by
 * name and returns its index, or -1. */
static long long find_field(const Decl *decl, const char *name) {
    for (size_t i = 0; i < decl->node->field_count; i++) {
        if (strcmp(decl->node->fields[i].name, name) == 0) return (long long)i;
    }
    return -1;
}

static void free_aggregate_fields(Value *value) {
    if (value->as.aggregate.fields == NULL) return;
    /* Field values may own strings; release them rather than leaking. */
    for (size_t i = 0; i < value->as.aggregate.decl->node->field_count; i++) {
        if (value->as.aggregate.fields[i].type == VALUE_STRING &&
            value->as.aggregate.fields[i].as.string.chars != NULL) {
            /* Only strings the interpreter allocated are freed; ones borrowed
             * from AST nodes must not be. Phase 5 owns that distinction. */
        }
    }
    free(value->as.aggregate.fields);
    value->as.aggregate.fields = NULL;
}

/* Builds an aggregate value with every field initialised to Nothing, then the
 * caller fills the ones the literal names. */
static bool make_aggregate(InterpState *state, const Decl *decl, Position position,
                           Value *out) {
    size_t count = decl->node->field_count;
    memset(out, 0, sizeof(*out));
    out->type = decl->is_union ? VALUE_UNION : VALUE_STRUCT;
    out->as.aggregate.decl = decl;
    out->as.aggregate.tag = -1;
    out->as.aggregate.fields = calloc(count, sizeof(Value));
    if (out->as.aggregate.fields == NULL) {
        fail(state, position, "out of memory");
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        out->as.aggregate.fields[i] = value_nil();
    }
    return true;
}

/* `Type { field: value, ... }` */
static bool evaluate_struct_init(InterpState *state, const Expr *expression,
                                 Scope *scope, Value *out) {
    const Decl *decl = find_decl(state, expression->name);
    if (decl == NULL) {
        fail(state, expression->position, "undefined struct '%s'", expression->name);
        return false;
    }
    if (decl->is_union) {
        fail(state, expression->position,
             "'%s' is a union; construct it with '%s.Variant(...)'",
             expression->name, expression->name);
        return false;
    }

    Value result;
    if (!make_aggregate(state, decl, expression->position, &result)) return false;

    bool seen[256] = {false};

    for (size_t i = 0; i < expression->argument_count; i++) {
        const Expr *field = expression->arguments[i];
        long long index = find_field(decl, field->name);
        if (index < 0) {
            fail(state, field->position, "'%s' has no field '%s'",
                 expression->name, field->name);
            free_aggregate_fields(&result);
            return false;
        }
        if (seen[index]) {
            fail(state, field->position, "field '%s' is set twice", field->name);
            free_aggregate_fields(&result);
            return false;
        }
        seen[index] = true;

        Value value;
        if (!evaluate(state, field->right, scope, &value)) {
            free_aggregate_fields(&result);
            return false;
        }

        /* Field types are enforced, which is what stops a struct from quietly
         * holding a Character where a Word is declared. */
        TypeKind expected = decl->node->fields[index].type;
        if (!value_matches_type(value, expected)) {
            if (value.type == VALUE_INTEGER &&
                value.as.integer.type == TYPE_NUMBER &&
                integer_fits(value.as.integer.bits, expected)) {
                value = value_integer(value.as.integer.bits, expected);
            } else {
                fail(state, field->position,
                     "field '%s' of '%s' expects %s, got %s",
                     field->name, expression->name,
                     type_kind_name(expected), type_name_of(value));
                free_aggregate_fields(&result);
                return false;
            }
        }
        result.as.aggregate.fields[index] = value;
    }

    /* Every field must be given a value: a partially initialised struct is a
     * bug source, and Nothing-as-placeholder hides it until much later. */
    for (size_t i = 0; i < decl->node->field_count; i++) {
        if (!seen[i]) {
            fail(state, expression->position,
                 "'%s' is missing a value for field '%s'",
                 expression->name, decl->node->fields[i].name);
            free_aggregate_fields(&result);
            return false;
        }
    }

    *out = result;
    return true;
}

/* `Type.Variant` or `Type.Variant(payload)`.
 *
 * Arrives as an EXPR_FIELD whose left side is the type name and whose `name` is
 * the variant -- the parser cannot tell `Type.Variant` from `value.field`
 * syntactically, so evaluate_field routes here once it knows the left side
 * names a type. */
static bool evaluate_union_init(InterpState *state, const Expr *expression,
                                Scope *scope, Value *out) {
    const char *type_name = expression->left->name;
    const char *variant_name = expression->name;

    const Decl *decl = find_decl(state, type_name);
    if (decl == NULL) {
        fail(state, expression->position, "undefined type '%s'", type_name);
        return false;
    }
    if (!decl->is_union) {
        fail(state, expression->position,
             "'%s' is a struct; construct it with '%s { ... }'", type_name, type_name);
        return false;
    }

    long long index = find_field(decl, variant_name);
    if (index < 0) {
        fail(state, expression->position, "'%s' has no variant '%s'",
             type_name, variant_name);
        return false;
    }

    const Field *variant = &decl->node->fields[index];

    if (variant->has_payload != (expression->payload != NULL)) {
        if (variant->has_payload) {
            fail(state, expression->position,
                 "variant '%s' of '%s' needs a payload", variant_name, type_name);
        } else {
            fail(state, expression->position,
                 "variant '%s' of '%s' takes no payload", variant_name, type_name);
        }
        return false;
    }

    Value result;
    if (!make_aggregate(state, decl, expression->position, &result)) return false;

    if (expression->payload != NULL) {
        Value payload;
        if (!evaluate(state, expression->payload, scope, &payload)) {
            free_aggregate_fields(&result);
            return false;
        }
        if (!value_matches_type(payload, variant->type)) {
            if (payload.type == VALUE_INTEGER &&
                payload.as.integer.type == TYPE_NUMBER &&
                integer_fits(payload.as.integer.bits, variant->type)) {
                payload = value_integer(payload.as.integer.bits, variant->type);
            } else {
                fail(state, expression->position,
                     "variant '%s' of '%s' expects %s, got %s",
                     variant_name, type_name,
                     type_kind_name(variant->type), type_name_of(payload));
                free_aggregate_fields(&result);
                return false;
            }
        }
        result.as.aggregate.fields[index] = payload;
    }

    result.as.aggregate.tag = index;
    *out = result;
    return true;
}

/* `expr.field` or `Type.Variant(payload)`.
 *
 * Both have the same syntax, so this resolves the ambiguity once the left side
 * is known: a type name means a union construction, a value means a field read.
 * Doing it here rather than in the parser is the only place the answer exists. */
static bool evaluate_field(InterpState *state, const Expr *expression,
                           Scope *scope, Value *out) {
    /* A union construction when the left side names a type rather than a
     * value. `expression->left` is a bare identifier in that case, which is the
     * one situation the parser can recognise without a symbol table. */
    if (expression->left != NULL && expression->left->type == EXPR_IDENT) {
        bool names_a_value = scope_get(scope, expression->left->name, &(Value){0});
        if (!names_a_value) {
            /* Not a variable, so this is `Type.Variant`. */
            return evaluate_union_init(state, expression, scope, out);
        }
    }

    Value base;
    if (!evaluate(state, expression->left, scope, &base)) return false;

    if (base.type == VALUE_UNION) {
        fail(state, expression->position,
             "cannot read field '%s' from the union '%s'; use Match to read its variant",
             expression->name, base.as.aggregate.decl->node->name);
        return false;
    }

    if (base.type != VALUE_STRUCT) {
        fail(state, expression->position,
             "cannot read field '%s' from %s", expression->name, type_name_of(base));
        return false;
    }

    long long index = find_field(base.as.aggregate.decl, expression->name);
    if (index < 0) {
        fail(state, expression->position, "'%s' has no field '%s'",
             base.as.aggregate.decl->node->name, expression->name);
        return false;
    }

    *out = base.as.aggregate.fields[index];
    return true;
}

/* Field assignment: `a.field = value`.
 *
 * The target is stored as an EXPR_FIELD, so evaluating it would yield the
 * field's current value rather than the struct it belongs to. The struct is
 * reached through `field->left` instead -- the object the field is read from. */
/* Field assignment: `a.field = value`.
 *
 * The object is read through `field->left` *without copying*, so the write
 * lands on the variable's own storage. Reading it through the ordinary
 * identifier path would return a copy, and the assignment would be written into
 * that copy and then discarded -- `b.x = 99` would leave both `a` and `b`
 * unchanged, or with a copy-on-read scheme, change both.
 *
 * Copying on read is still correct for everything that only reads a struct.
 */
static bool assign_field(InterpState *state, const Expr *expression,
                         Scope *scope, Value value) {
    const Expr *field = expression->left;
    if (field == NULL || field->left == NULL) {
        fail(state, expression->position, "malformed field assignment");
        return false;
    }

    /* Only a bare identifier is supported as an assignment target. Anything
     * deeper (`a.b.x = 1`) needs lvalue tracking that does not exist yet, and
     * guessing would silently write to the wrong place. */
    if (field->left->type != EXPR_IDENT) {
        fail(state, expression->position,
             "cannot assign to a nested field yet");
        return false;
    }

    Value *stored = scope_find(scope, field->left->name);
    if (stored == NULL) {
        fail(state, expression->position, "undefined variable '%s'",
             field->left->name);
        return false;
    }
    if (stored->type != VALUE_STRUCT) {
        fail(state, expression->position,
             "cannot assign to a field of %s", type_name_of(*stored));
        return false;
    }

    long long index = find_field(stored->as.aggregate.decl, field->name);
    if (index < 0) {
        fail(state, expression->position, "'%s' has no field '%s'",
             stored->as.aggregate.decl->node->name, field->name);
        return false;
    }

    /* The declared field type is enforced on assignment as well as on
     * construction, so a field cannot change type midway through a program. */
    TypeKind expected = stored->as.aggregate.decl->node->fields[index].type;
    if (!value_matches_type(value, expected)) {
        if (value.type == VALUE_INTEGER && value.as.integer.type == TYPE_NUMBER &&
            integer_fits(value.as.integer.bits, expected)) {
            value = value_integer(value.as.integer.bits, expected);
        } else {
            fail(state, expression->position, "field '%s' expects %s, got %s",
                 field->name, type_kind_name(expected), type_name_of(value));
            return false;
        }
    }

    stored->as.aggregate.fields[index] = value;
    return true;
}

/* --- Errors: Try, Catch, and propagation ---------------------------------- */

/* Wraps a value as a successful Result. */
static bool make_result_ok(Value inner, Value *out) {
    out->type = VALUE_RESULT;
    memset(&out->as, 0, sizeof(out->as));
    out->as.result.ok = true;
    out->as.result.value = malloc(sizeof(Value));
    if (out->as.result.value == NULL) return false;
    *out->as.result.value = inner;
    return true;
}

/* Builds a failure Result carrying one variant of `decl`. */
static bool make_result_error(const Decl *decl, size_t variant, Value *out) {
    out->type = VALUE_RESULT;
    memset(&out->as, 0, sizeof(out->as));
    out->as.result.ok = false;
    out->as.result.error_decl = decl;
    out->as.result.error_variant = variant;
    return true;
}

/* The error set a function declared, or NULL when it is infallible.
 *
 * Resolution happens here rather than at parse time because an Error
 * declaration may follow the function that uses it, the same rule that lets a
 * function be called before the line that defines it. */
static const Decl *function_error_set(const InterpState *state, const Stmt *declaration) {
    if (declaration == NULL || declaration->declared_type != TYPE_RESULT) return NULL;
    return find_decl(state, declaration->result_error_name);
}

/* True when `outer` can carry every error `inner` can produce. A callee's
 * error set is a subset of the caller's -- the caller must be able to name
 * whatever the callee might raise. */
static bool error_set_covers(const Decl *outer, const Decl *inner) {
    if (inner == NULL) return true;
    if (outer == NULL) return false;
    for (size_t i = 0; i < inner->node->field_count; i++) {
        if (find_field(outer, inner->node->fields[i].name) < 0) return false;
    }
    return true;
}

/* Names the variants of `inner` that `outer` cannot carry. Used to make the
 * diagnostic say what to add rather than only that something is wrong. */
static void report_missing_errors(InterpState *state, Position position,
                                  const char *callee, const Decl *inner,
                                  const Decl *outer) {
    char missing[192];
    size_t used = 0;
    missing[0] = '\0';

    for (size_t i = 0; i < inner->node->field_count; i++) {
        const char *name = inner->node->fields[i].name;
        if (outer != NULL && find_field(outer, name) >= 0) continue;
        int written = snprintf(missing + used, sizeof(missing) - used, "%s%s",
                               used > 0 ? ", " : "", name);
        if (written < 0 || (size_t)written >= sizeof(missing) - used) break;
        used += (size_t)written;
    }

    fail(state, position,
         "'%s' can fail with '%s', which '%s' does not declare",
         callee, missing,
         state->current_function != NULL ? state->current_function->name : "this scope");
}

/* The fallible expression under a Try or Catch, resolved to its declaration.
 * Both operators require a call, because both need to know the callee's error
 * set, and a call is the only thing that has one. */
static bool require_fallible_call(InterpState *state, const Expr *expression,
                                  const Expr **call, Position position) {
    const Expr *operand = expression->left;
    if (operand == NULL || operand->type != EXPR_CALL ||
        operand->callee == NULL || operand->callee->type != EXPR_IDENT) {
        fail(state, position, "Try and Catch apply to a function call");
        return false;
    }
    *call = operand;
    return true;
}

/* `expr Try` -- on failure, return the error from the enclosing function.
 *
 * Exactly Zig's `catch |err| return err`, spelled so there is one form of it.
 * The rule that matters is the one enforced here: the enclosing function must
 * declare an error set that covers the callee's, or the error would leave a
 * function whose signature cannot describe it. */
static bool evaluate_try(InterpState *state, const Expr *expression, Scope *scope,
                         Value *out) {
    const Expr *call = NULL;
    if (!require_fallible_call(state, expression, &call, expression->position)) return false;

    const char *callee = call->callee->name;
    const Function *function = find_function(state, callee);
    if (function == NULL) {
        fail(state, expression->position, "undefined function '%s'", callee);
        return false;
    }

    const Decl *callee_errors = function_error_set(state, function->declaration);
    if (callee_errors == NULL) {
        fail(state, expression->position, "'%s' cannot fail, so Try has nothing to do",
             callee);
        return false;
    }

    if (state->current_function == NULL) {
        fail(state, expression->position,
             "'%s' fails and there is no enclosing function to propagate to", callee);
        return false;
    }

    const Decl *caller_errors =
        function_error_set(state, state->current_function);
    if (!error_set_covers(caller_errors, callee_errors)) {
        report_missing_errors(state, expression->position, callee,
                              callee_errors, caller_errors);
        return false;
    }

    Value value;
    if (!evaluate(state, call, scope, &value)) return false;
    if (value.type != VALUE_RESULT) {
        fail(state, expression->position, "internal error: Try on a non-Result");
        return false;
    }

    if (value.as.result.ok) {
        *out = *value.as.result.value;
        free(value.as.result.value);
        return true;
    }

    /* The failure belongs to the caller now. The flag is read by the statement
     * loop and cleared at the call boundary that owns it. */
    state->propagation.propagating = true;
    state->propagation.error = value;
    state->propagation.propagating_from = function->declaration;
    *out = value_nil();
    return true;
}

/* `expr Catch { Variant { ... } ... }` -- handle the failure locally.
 *
 * Exhaustiveness is checked here, and it is the deliberate point of departure
 * from Zig, whose idiom is `catch |err| switch (err) { ... else => ... }` --
 * where `else` compiles today, keeps compiling when a variant is added, and
 * silently routes a new failure into whatever fallback was written for a
 * different one. See research/design/error-syntax.md. */
static bool evaluate_catch(InterpState *state, const Expr *expression, Scope *scope,
                           Value *out) {
    const Expr *call = NULL;
    if (!require_fallible_call(state, expression, &call, expression->position)) return false;

    const char *callee = call->callee->name;
    const Function *function = find_function(state, callee);
    if (function == NULL) {
        fail(state, expression->position, "undefined function '%s'", callee);
        return false;
    }

    const Decl *errors = function_error_set(state, function->declaration);
    if (errors == NULL) {
        fail(state, expression->position, "'%s' cannot fail, so Catch has nothing to do",
             callee);
        return false;
    }

    /* Names are checked before the call runs, so a mistake in the handler is
     * reported without having done the fallible work first. */
    for (size_t i = 0; i < expression->arm_count; i++) {
        const MatchArm *arm = &expression->arms[i];
        if (find_field(errors, arm->variant) < 0) {
            fail(state, arm->position, "'%s' cannot fail with '%s'", callee, arm->variant);
            return false;
        }
        for (size_t j = 0; j < i; j++) {
            if (strcmp(expression->arms[j].variant, arm->variant) == 0) {
                fail(state, arm->position, "'%s' is handled twice", arm->variant);
                return false;
            }
        }
    }

    for (size_t i = 0; i < errors->node->field_count; i++) {
        const char *name = errors->node->fields[i].name;
        bool handled = false;
        for (size_t j = 0; j < expression->arm_count && !handled; j++) {
            handled = strcmp(expression->arms[j].variant, name) == 0;
        }
        if (!handled) {
            fail(state, expression->position,
                 "Catch does not handle '%s' of '%s'", name, errors->node->name);
            return false;
        }
    }

    Value value;
    if (!evaluate(state, call, scope, &value)) return false;
    if (value.type != VALUE_RESULT) {
        fail(state, expression->position, "internal error: Catch on a non-Result");
        return false;
    }

    if (value.as.result.ok) {
        *out = *value.as.result.value;
        free(value.as.result.value);
        return true;
    }

    const Decl *failed = value.as.result.error_decl;
    size_t index = value.as.result.error_variant;
    if (failed == NULL || index >= failed->node->field_count) {
        fail(state, expression->position, "internal error: bad error reference");
        return false;
    }

    /* Every arm was checked above, so this always finds one. */
    for (size_t i = 0; i < expression->arm_count; i++) {
        const MatchArm *arm = &expression->arms[i];
        if (find_field(failed, arm->variant) != (long long)index) continue;

        Scope *inner = scope_create(scope);
        if (inner == NULL) {
            fail(state, arm->position, "out of memory");
            return false;
        }

        if (arm->binding != NULL) {
            fail(state, arm->position,
                 "'%s' carries no payload yet; binding it is not possible",
                 arm->variant);
            scope_destroy(inner);
            return false;
        }

        Flow flow = {0};
        bool ok = true;
        bool produced = false;
        Value arm_value = value_nil();

        /* The arm is executed here rather than through execute_block because
         * the block's value is the value of its last *expression statement*.
         * execute_block discards expression statements -- it is written for
         * bodies, where a bare expression is a call for its effect. Reading the
         * last expression here makes the rule from the design explicit at the
         * one place it applies. */
        for (size_t i = 0; i < arm->body_count; i++) {
            const Stmt *body_statement = arm->body[i];
            if (body_statement->type == STMT_EXPR) {
                if (!evaluate(state, body_statement->expression, inner, &arm_value)) {
                    ok = false;
                    break;
                }
                produced = true;
            } else {
                if (!execute_statement(state, body_statement, inner, &flow)) {
                    ok = false;
                    break;
                }
                if (flow.returned || state->propagation.propagating) break;
            }
        }

        /* The parser already rejected an arm that does not end with an
         * expression, so this only fires if the grammar and this loop drift
         * apart -- worth a message rather than a Nothing. */
        if (ok && !produced) {
            fail(state, arm->position,
                 "Catch arm '%s' must end with an expression", arm->variant);
            ok = false;
        }
        if (ok) *out = arm_value;
        scope_destroy(inner);
        return ok;
    }

    fail(state, expression->position,
         "internal error: no arm for the failing error");
    return false;
}

static bool evaluate(InterpState *state, const Expr *expression,
                     Scope *scope, Value *out) {
    if (expression == NULL) {
        *out = value_nil();
        return true;
    }

    switch (expression->type) {
        case EXPR_NUMBER:
            /* A literal has no type until something gives it one. It arrives
             * here as a Number and is narrowed by the operation that consumes
             * it -- see adopt_operand_type in evaluate_binary -- so that
             * `w - 30` works when w is a Word. */
            *out = value_number(expression->number);
            return true;

        case EXPR_CHARACTER:
            *out = value_integer((unsigned char)expression->text[0], TYPE_CHARACTER);
            return true;

        /* True and False are Number 1 and 0 (decision 17). They stay distinct
         * nodes so that error messages can quote the source form. */
        case EXPR_BOOL: {
            /* Number 1 and 0 (decision 17), carrying the source form so that
             * Terminal.Write can print True/False rather than 1/0. The tag is
             * presentation only: truthiness still follows the number.
             *
             * Both True and False set the flag, including False whose value is
             * 0. Using the value itself as the flag would make False
             * indistinguishable from an ordinary 0, which is the bug this
             * shape avoids. */
            Value value = value_number(expression->boolean ? 1 : 0);
            value.is_bool_literal = true;
            *out = value;
            return true;
        }

        case EXPR_STRING:
            *out = value_string(expression->text, expression->text_length);
            return true;

        case EXPR_IDENT: {
            Value found;
            if (scope_get(scope, expression->name, &found)) {
                *out = found;
                return true;
            }
            fail(state, expression->position, "undefined variable '%s'", expression->name);
            return false;
        }

        case EXPR_UNARY:
            return evaluate_unary(state, expression, scope, out);

        case EXPR_BINARY:
            return evaluate_binary(state, expression, scope, out);

        case EXPR_ASSIGN: {
            Value value;
            if (!evaluate(state, expression->right, scope, &value)) return false;

            if (expression->left != NULL) {
                /* A field assignment; an OS does this constantly. */
                if (!assign_field(state, expression, scope, value)) return false;
                *out = value;
                return true;
            }

            /* The literal form is dropped on the way in: a stored variable
             * holds a Number, so Terminal.Write(True) prints True while
             * Terminal.Write(x) where x = True prints 1. */
            value = value_as_plain(value);
            if (!scope_assign(scope, expression->name, value)) {
                fail(state, expression->position,
                     "undefined variable '%s'", expression->name);
                return false;
            }
            *out = value;
            return true;
        }

        case EXPR_BUILTIN:
            return evaluate_builtin(state, expression, scope, out);

        case EXPR_STRUCT_INIT:
            return evaluate_struct_init(state, expression, scope, out);

        case EXPR_UNION_INIT:
            return evaluate_union_init(state, expression, scope, out);

        case EXPR_FIELD:
            return evaluate_field(state, expression, scope, out);

        case EXPR_TRY:
            return evaluate_try(state, expression, scope, out);

        case EXPR_CATCH:
            return evaluate_catch(state, expression, scope, out);

        case EXPR_CALL:
            return evaluate_call(state, expression, scope, out);

        default:
            fail(state, expression->position, "unhandled expression");
            return false;
    }
}

/* --- Statements ------------------------------------------------------------ */

static bool execute_block(InterpState *state, Stmt *const *body, size_t count,
                          Scope *scope, Flow *flow) {
    for (size_t i = 0; i < count; i++) {
        if (!execute_statement(state, body[i], scope, flow)) return false;
        /* A propagating error leaves the function just as a Return does. Every
         * loop over statements needs both conditions, or a Try inside a nested
         * block would keep running the statements after it. */
        if (flow->returned || state->propagation.propagating) break;
    }
    return true;
}

static bool execute_statement(InterpState *state, const Stmt *statement,
                              Scope *scope, Flow *flow) {
    /* Once an error is propagating, this function is on its way out. No further
     * statement may run, whatever it would have done. Checked here rather than
     * only at the loop so that statements reached by nesting -- an If body, a
     * loop body, a nested block -- stop too. The loop checks alone would let
     * the remainder of an enclosing block run. */
    if (state->propagation.propagating) return true;

    switch (statement->type) {
        case STMT_EXPR: {
            Value ignored;
            return evaluate(state, statement->expression, scope, &ignored);
        }

        case STMT_VARIABLE: {
            Value initial = value_nil();
            if (statement->initializer != NULL) {
                if (!evaluate(state, statement->initializer, scope, &initial)) return false;

                /* A Try inside the initialiser may have started an error
                 * propagation. The declaration must not complete on the Nothing
                 * that Try returned as a placeholder -- the function is already
                 * on its way out, and defining a variable here would both
                 * mis-type it and run code that should never run. */
                if (state->propagation.propagating) return true;

                /* An integer literal takes the declared type when it fits.
                 *
                 * Narrowing is allowed only from Number to Character and from
                 * Number to Number32 -- both are literal-width choices, since a
                 * literal has no type of its own until something gives it one.
                 * Crossing to Word is refused: silently turning a signed literal
                 * into an unsigned one is the mistake this whole change exists
                 * to prevent, so `Variable n: Word = -1` must be written with an
                 * explicit conversion. */
                TypeKind expected = statement->declared_type;
                if (expected == TYPE_NAMED) {
                    /* A Struct or Union. Compared by identity against the
                     * declaration rather than by type name, so two aggregates
                     * with the same field names are still distinct. */
                    const Decl *decl = resolve_named(state, statement->type_name);
                    if (decl == NULL) {
                        fail(state, statement->position,
                             "undefined type '%s'", statement->type_name);
                        return false;
                    }
                    if (initial.type != (decl->is_union ? VALUE_UNION : VALUE_STRUCT) ||
                        initial.as.aggregate.decl != decl) {
                        fail(state, statement->position,
                             "cannot initialise '%s' of type %s with %s",
                             statement->name, statement->type_name,
                             type_name_of(initial));
                        return false;
                    }
                } else if (!value_matches_type(initial, expected)) {
                    TypeKind given = initial.type == VALUE_INTEGER
                                         ? initial.as.integer.type
                                         : TYPE_NUMBER;
                    bool literal_narrowing = initial.type == VALUE_INTEGER &&
                                             given == TYPE_NUMBER &&
                                             integer_fits(initial.as.integer.bits, expected);

                    if (literal_narrowing) {
                        initial = value_integer(initial.as.integer.bits, expected);
                    } else {
                        fail(state, statement->position,
                             "cannot initialise '%s' of type %s with %s",
                             statement->name, type_kind_name(expected),
                             type_name_of(initial));
                        return false;
                    }
                }
            }

            if (scope_contains(scope, statement->name)) {
                fail(state, statement->position, "'%s' is already declared in this scope",
                     statement->name);
                return false;
            }
            /* Copied, so `Variable b: P = a` does not leave b sharing a's fields.
             * Structs and unions are value types; without this, assigning a
             * field through either variable changes both. */
            if (!scope_define(scope, statement->name, value_copy(initial))) {
                fail(state, statement->position, "out of memory");
                return false;
            }
            return true;
        }

        case STMT_BLOCK: {
            /* A nested block gets its own scope, so a variable declared inside
             * a loop body does not leak into the enclosing function. */
            Scope *inner = scope_create(scope);
            if (inner == NULL) {
                fail(state, statement->position, "out of memory");
                return false;
            }
            bool ok = execute_block(state, statement->body, statement->body_count, inner, flow);
            scope_destroy(inner);
            return ok;
        }

        case STMT_IF: {
            Value condition;
            if (!evaluate(state, statement->expression, scope, &condition)) return false;
            /* C-style truthiness (decision 17): any integer, zero being False.
             * A String condition is a type error, since there is no reasonable
             * reading of `If ("x")`. */
            if (condition.type != VALUE_INTEGER) {
                fail(state, statement->position,
                     "condition of 'If' must be an integer, got %s",
                     type_name_of(condition));
                return false;
            }
            if (is_truthy(condition)) {
                return execute_block(state, statement->then_body, statement->then_count,
                                     scope, flow);
            }
            if (statement->else_branch != NULL) {
                return execute_statement(state, statement->else_branch, scope, flow);
            }
            return true;
        }

        case STMT_LOOP: {
            while (true) {
                Value condition;
                if (!evaluate(state, statement->expression, scope, &condition)) return false;
                if (condition.type != VALUE_INTEGER) {
                    fail(state, statement->position,
                         "condition of 'Loop' must be an integer, got %s",
                         type_name_of(condition));
                    return false;
                }
                if (!is_truthy(condition)) break;

                Scope *inner = scope_create(scope);
                if (inner == NULL) {
                    fail(state, statement->position, "out of memory");
                    return false;
                }
                bool ok = execute_block(state, statement->loop_body_statements,
                                        statement->loop_count, inner, flow);
                scope_destroy(inner);
                if (!ok) return false;
                if (flow->returned || state->propagation.propagating) break;
            }
            return true;
        }

        case STMT_RETURN:
            flow->returned = true;
            flow->return_value = value_nil();
            if (statement->expression != NULL) {
                if (!evaluate(state, statement->expression, scope, &flow->return_value)) {
                    return false;
                }
                /* Same as a variable initialiser: a propagating Try must not
                 * then be treated as an ordinary Return of Nothing. */
                if (state->propagation.propagating) flow->return_value = value_nil();
            }
            return true;

        case STMT_FAIL: {
            /* The error set is fixed by the enclosing signature, so a bare
             * variant name is unambiguous and the set is not repeated here. */
            if (state->current_function == NULL) {
                fail(state, statement->position,
                     "'Fail' needs an enclosing function to fail");
                return false;
            }

            const Decl *errors =
                function_error_set(state, state->current_function);
            if (errors == NULL) {
                fail(state, statement->position,
                     "'%s' cannot fail; it declares no error set",
                     state->current_function->name);
                return false;
            }

            long long index = find_field(errors, statement->name);
            if (index < 0) {
                fail(state, statement->position,
                     "'%s' cannot fail with '%s'", state->current_function->name,
                     statement->name);
                return false;
            }

            Value error;
            if (!make_result_error(errors, (size_t)index, &error)) {
                fail(state, statement->position, "out of memory");
                return false;
            }
            state->propagation.propagating = true;
            state->propagation.error = error;
            state->propagation.propagating_from = state->current_function;
            flow->returned = true;
            flow->return_value = value_nil();
            return true;
        }

        case STMT_STRUCT:
        case STMT_UNION:
            /* Types are registered in a pre-pass, so reaching the declaration
             * here means it is a duplicate. */
            fail(state, statement->position,
                 "internal error: '%s' registered twice", statement->name);
            return false;

        case STMT_MATCH: {
            Value subject;
            if (!evaluate(state, statement->expression, scope, &subject)) return false;

            if (subject.type != VALUE_UNION) {
                fail(state, statement->position,
                     "Match needs a union value, got %s", type_name_of(subject));
                return false;
            }

            const Decl *decl = subject.as.aggregate.decl;
            long long active = subject.as.aggregate.tag;

            /* The declared type name on the Match must match the value's, so a
             * typo in an arm is an error rather than a dead branch. */
            if (statement->name != NULL &&
                strcmp(statement->name, decl->node->name) != 0) {
                fail(state, statement->position,
                     "Match arms name '%s' but the value is a '%s'",
                     statement->name, decl->node->name);
                return false;
            }

            for (size_t i = 0; i < statement->arm_count; i++) {
                const MatchArm *arm = &statement->arms[i];
                long long index = find_field(decl, arm->variant);
                if (index < 0) {
                    fail(state, arm->position, "'%s' has no variant '%s'",
                         decl->node->name, arm->variant);
                    return false;
                }
                if (index != active) continue;

                /* The matched arm gets its own scope, and the payload is bound
                 * to the arm's name when it asked for one. */
                Scope *inner = scope_create(scope);
                if (inner == NULL) {
                    fail(state, arm->position, "out of memory");
                    return false;
                }

                const Field *variant = &decl->node->fields[index];
                if (arm->binding != NULL) {
                    if (!variant->has_payload) {
                        fail(state, arm->position,
                             "variant '%s' has no payload to bind to '%s'",
                             arm->variant, arm->binding);
                        scope_destroy(inner);
                        return false;
                    }
                    if (!scope_define(inner, arm->binding,
                                      subject.as.aggregate.fields[index])) {
                        fail(state, arm->position, "out of memory");
                        scope_destroy(inner);
                        return false;
                    }
                }

                bool ok = execute_block(state, arm->body, arm->body_count, inner, flow);
                scope_destroy(inner);
                return ok;
            }

            /* No arm matched. Exhaustiveness is checked when the Match is
             * reached, not just at parse time, so a value built dynamically
             * still cannot fall through silently. */
            fail(state, statement->position,
                 "Match on '%s' is not exhaustive: no arm handles variant '%s'",
                 decl->node->name, decl->node->fields[active].name);
            return false;
        }

        case STMT_FUNCTION:
            /* Function declarations are hoisted in a pre-pass before execution
             * (see interp_run), so reaching one here means a duplicate. */
            fail(state, statement->position,
                 "internal error: function '%s' registered twice", statement->name);
            return false;

        default:
            fail(state, statement->position, "unhandled statement");
            return false;
    }
}

/* --- Entry point ----------------------------------------------------------- */

/* Pass 1: register every function against the global scope, so calls resolve
 * regardless of declaration order. This is what makes recursion work. */
static bool hoist_functions(InterpState *state, const Program *program, Scope *global) {
    for (size_t i = 0; i < program->count; i++) {
        const Stmt *statement = program->statements[i];
        if (statement->type != STMT_FUNCTION) continue;

        const Function *existing = find_function(state, statement->name);
        if (existing != NULL) {
            fail(state, statement->position, "function '%s' is already defined",
                 statement->name);
            return false;
        }
        if (!register_function(state, statement, global)) return false;
    }
    return true;
}

/* Pass 0: register Struct and Union declarations, so a type may be used
 * before the line that declares it -- the same rule functions already follow. */
static bool register_decl(InterpState *state, const Stmt *node) {
    const Decl *existing = find_decl(state, node->name);
    if (existing != NULL) {
        fail(state, node->position, "'%s' is already declared", node->name);
        return false;
    }

    if (state->decl_count + 1 > state->decl_capacity) {
        size_t capacity = state->decl_capacity ? state->decl_capacity * 2 : 8;
        Decl *grown = realloc(state->decls, capacity * sizeof(Decl));
        if (grown == NULL) {
            fail(state, node->position, "out of memory");
            return false;
        }
        state->decls = grown;
        state->decl_capacity = capacity;
    }

    state->decls[state->decl_count].node = node;
    state->decls[state->decl_count].is_union = (node->type == STMT_UNION);
    state->decl_count++;
    return true;
}

static bool hoist_decls(InterpState *state, const Program *program) {
    for (size_t i = 0; i < program->count; i++) {
        const Stmt *statement = program->statements[i];
        if (statement->type != STMT_STRUCT && statement->type != STMT_UNION) {
            continue;
        }
        if (!register_decl(state, statement)) return false;
    }
    return true;
}

int interp_run(const Program *program, Interp *interp) {
    InterpState state = {0};
    state.program = program;

    state.global_scope = scope_create(NULL);
    if (state.global_scope == NULL) {
        fprintf(stderr, "siyana: out of memory\n");
        return 1;
    }

    if (hoist_decls(&state, program) && hoist_functions(&state, program, state.global_scope)) {
        for (size_t i = 0; i < program->count; i++) {
            const Stmt *statement = program->statements[i];
            /* Declarations were handled by the pre-passes. */
            if (statement->type == STMT_FUNCTION ||
                statement->type == STMT_STRUCT ||
                statement->type == STMT_UNION) {
                continue;
            }

            Flow flow = {0};
            if (!execute_statement(&state, statement, state.global_scope, &flow)) break;
        }
    }

    int result = 0;
    if (state.failed) {
        interp->had_error = true;
        interp->error_position = state.error_position;
        snprintf(interp->message, sizeof(interp->message), "%s", state.message);
        result = 1;
    }

    free(state.functions);
    scope_destroy_chain(state.global_scope);
    return result;
}
