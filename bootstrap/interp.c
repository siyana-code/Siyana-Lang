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

typedef struct {
    const Program *program;
    Scope *global_scope;

    Function *functions;
    size_t function_count;
    size_t function_capacity;

    bool failed;
    char message[160];
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
        default:             return "unknown";
    }
}

/* Pointer width of the build target. Word is the one type whose size is not
 * fixed, and that is exactly why it exists. */
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

/* --- Forward declarations -------------------------------------------------- */

static bool execute_statement(InterpState *state, const Stmt *statement,
                              Scope *scope, Flow *flow);
static bool evaluate(InterpState *state, const Expr *expression,
                     Scope *scope, Value *out);

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
        if (!value_matches_type(argument, expected)) {
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
        if (!scope_define(call_scope, parameter->name, argument)) {
            fail(state, parameter->position, "out of memory");
            scope_destroy(call_scope);
            return false;
        }
    }

    Flow flow = {0};
    for (size_t i = 0; i < declaration->body_count; i++) {
        if (!execute_statement(state, declaration->body[i], call_scope, &flow)) {
            scope_destroy(call_scope);
            return false;
        }
        if (flow.returned) break;
    }

    *out = flow.returned ? flow.return_value : value_nil();
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
        default:
            printf("nothing");
            break;
    }
}

static bool evaluate_builtin(InterpState *state, const Expr *expression,
                             Scope *scope, Value *out) {
    if (expression->builtin == TOKEN_WRITE) {
        if (expression->argument_count != 1) {
            fail(state, expression->position,
                 "Terminal.Write expects 1 argument, got %zu", expression->argument_count);
            return false;
        }
        Value argument;
        if (!evaluate(state, expression->arguments[0], scope, &argument)) return false;

        write_value(argument);

        /* Terminal.Write appends a newline, matching the behaviour of a shell
         * that writes one line at a time. A future WriteRaw writes without one;
         * that is a library addition, not a language one. */
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
    return ok;
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
            /* A stored variable holds a Number, so the literal form is dropped here.
             * Terminal.Write(True) prints True; Terminal.Write(x) where
             * x = True prints 1. */
            value = value_as_plain(value);
            if (!scope_assign(scope, expression->name, value)) {
                fail(state, expression->position, "undefined variable '%s'", expression->name);
                return false;
            }
            *out = value;
            return true;
        }

        case EXPR_BUILTIN:
            return evaluate_builtin(state, expression, scope, out);

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
        if (flow->returned) break;
    }
    return true;
}

static bool execute_statement(InterpState *state, const Stmt *statement,
                              Scope *scope, Flow *flow) {
    switch (statement->type) {
        case STMT_EXPR: {
            Value ignored;
            return evaluate(state, statement->expression, scope, &ignored);
        }

        case STMT_VARIABLE: {
            Value initial = value_nil();
            if (statement->initializer != NULL) {
                if (!evaluate(state, statement->initializer, scope, &initial)) return false;

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
                if (!value_matches_type(initial, expected)) {
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
            if (!scope_define(scope, statement->name, initial)) {
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
                if (flow->returned) break;
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
            }
            return true;

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

int interp_run(const Program *program, Interp *interp) {
    InterpState state = {0};
    state.program = program;

    state.global_scope = scope_create(NULL);
    if (state.global_scope == NULL) {
        fprintf(stderr, "siyana: out of memory\n");
        return 1;
    }

    if (hoist_functions(&state, program, state.global_scope)) {
        for (size_t i = 0; i < program->count; i++) {
            const Stmt *statement = program->statements[i];
            if (statement->type == STMT_FUNCTION) continue;

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
