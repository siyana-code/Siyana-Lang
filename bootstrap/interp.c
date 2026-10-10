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
    Stmt *declaration;   /* STMT_FN */
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

static const char *type_name_of(Value value) {
    switch (value.type) {
        case VALUE_NIL:    return "nothing";
        case VALUE_BOOL:   return "bool";
        case VALUE_INT:    return "int";
        case VALUE_STRING: return "string";
        default:           return "unknown";
    }
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

static Value value_bool(bool boolean) {
    Value value = {0};
    value.type = VALUE_BOOL;
    value.as.boolean = boolean;
    return value;
}

static Value value_int(long long integer) {
    Value value = {0};
    value.type = VALUE_INT;
    value.as.integer = integer;
    return value;
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
        case OP_ADD:          return "+";
        case OP_SUBTRACT:     return "-";
        case OP_MULTIPLY:     return "*";
        case OP_DIVIDE:       return "/";
        case OP_EQUAL:        return "==";
        case OP_NOT_EQUAL:    return "!=";
        case OP_LESS:         return "<";
        case OP_LESS_EQUAL:   return "<=";
        case OP_GREATER:      return ">";
        case OP_GREATER_EQUAL: return ">=";
        default:              return "?";
    }
}

static bool evaluate_binary(InterpState *state, const Expr *expression,
                            Scope *scope, Value *out) {
    Value left;
    Value right;
    if (!evaluate(state, expression->left, scope, &left)) return false;
    if (!evaluate(state, expression->right, scope, &right)) return false;

    /* Equality works across matching types. Mismatched types are an error
     * rather than silently false: a type error here almost always means a
     * mistake, and reporting it is the compiler's job in the language's spirit. */
    if (expression->op == OP_EQUAL || expression->op == OP_NOT_EQUAL) {
        if (left.type == VALUE_STRING && right.type == VALUE_STRING) {
            bool equal = strings_equal(left, right);
            *out = value_bool(expression->op == OP_EQUAL ? equal : !equal);
            return true;
        }
        if (left.type != right.type) {
            fail(state, expression->position,
                 "cannot compare %s with %s using '%s'",
                 type_name_of(left), type_name_of(right), operator_symbol(expression->op));
            return false;
        }
        bool equal = false;
        switch (left.type) {
            case VALUE_INT:  equal = left.as.integer == right.as.integer; break;
            case VALUE_BOOL: equal = left.as.boolean == right.as.boolean; break;
            case VALUE_NIL:  equal = true; break;
            default: break;
        }
        *out = value_bool(expression->op == OP_EQUAL ? equal : !equal);
        return true;
    }

    /* The remaining operators are int-only in Phase 2. */
    if (left.type != VALUE_INT || right.type != VALUE_INT) {
        fail(state, expression->position,
             "operator '%s' requires int operands, got %s and %s",
             operator_symbol(expression->op), type_name_of(left), type_name_of(right));
        return false;
    }

    long long a = left.as.integer;
    long long b = right.as.integer;
    long long result = 0;

    switch (expression->op) {
        case OP_ADD:      result = a + b; break;
        case OP_SUBTRACT: result = a - b; break;
        case OP_MULTIPLY: result = a * b; break;
        case OP_DIVIDE:
            if (b == 0) {
                fail(state, expression->position, "division by zero");
                return false;
            }
            result = a / b;
            break;
        case OP_LESS:         *out = value_bool(a < b);     return true;
        case OP_LESS_EQUAL:   *out = value_bool(a <= b);    return true;
        case OP_GREATER:      *out = value_bool(a > b);     return true;
        case OP_GREATER_EQUAL: *out = value_bool(a >= b);    return true;
        default:
            fail(state, expression->position, "unhandled operator '%s'",
                 operator_symbol(expression->op));
            return false;
    }

    *out = value_int(result);
    return true;
}

static bool evaluate_unary(InterpState *state, const Expr *expression,
                            Scope *scope, Value *out) {
    Value operand;
    if (!evaluate(state, expression->right, scope, &operand)) return false;

    if (expression->op == OP_NOT) {
        if (operand.type != VALUE_BOOL) {
            fail(state, expression->position,
                 "operator '!' requires a bool operand, got %s", type_name_of(operand));
            return false;
        }
        *out = value_bool(!operand.as.boolean);
        return true;
    }

    if (operand.type != VALUE_INT) {
        fail(state, expression->position,
             "operator '-' requires an int operand, got %s", type_name_of(operand));
        return false;
    }
    *out = value_int(-operand.as.integer);
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
        Value argument = arguments[i];
        TypeKind expected = parameter->type;
        bool matches = (expected == TYPE_INT && argument.type == VALUE_INT) ||
                       (expected == TYPE_BOOL && argument.type == VALUE_BOOL);
        if (!matches) {
            fail(state, parameter->position,
                 "parameter '%s' of '%s' expects %s, got %s",
                 parameter->name, declaration->name,
                 expected == TYPE_INT ? "int" : "bool", type_name_of(argument));
            scope_destroy(call_scope);
            return false;
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

static bool evaluate_call(InterpState *state, const Expr *expression,
                          Scope *scope, Value *out) {
    /* A NULL callee marks a builtin call produced by `print`, which the parser
     * recognises as a keyword rather than an identifier. Phase 5 replaces this
     * with a real namespace lookup. */
    if (expression->callee == NULL) {
        if (expression->argument_count != 1) {
            fail(state, expression->position,
                 "print expects 1 argument, got %zu", expression->argument_count);
            return false;
        }
        Value argument;
        if (!evaluate(state, expression->arguments[0], scope, &argument)) return false;
        switch (argument.type) {
            case VALUE_INT:  printf("%lld\n", argument.as.integer); break;
            case VALUE_BOOL: printf("%s\n", argument.as.boolean ? "true" : "false"); break;
            case VALUE_STRING:
                printf("%.*s\n", (int)argument.as.string.length, argument.as.string.chars);
                break;
            default: printf("nothing\n"); break;
        }
        *out = value_nil();
        return true;
    }

    if (expression->callee->type != EXPR_IDENT) {
        fail(state, expression->position, "can only call named functions in Phase 2");
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
        case EXPR_INT:
            *out = value_int(expression->integer);
            return true;

        case EXPR_BOOL:
            *out = value_bool(expression->boolean);
            return true;

        case EXPR_STRING:
            *out = value_string(expression->string, strlen(expression->string));
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
            if (!scope_assign(scope, expression->name, value)) {
                fail(state, expression->position, "undefined variable '%s'", expression->name);
                return false;
            }
            *out = value;
            return true;
        }

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

        case STMT_VAR: {
            Value initial = value_nil();
            if (statement->initializer != NULL) {
                if (!evaluate(state, statement->initializer, scope, &initial)) return false;

                /* The declared type is checked against the initialiser. A
                 * mismatch here is reported as a type error naming both, which
                 * is the diagnostic the Phase 2 exit criteria ask for. */
                TypeKind expected = statement->declared_type;
                bool matches = (expected == TYPE_INT && initial.type == VALUE_INT) ||
                               (expected == TYPE_BOOL && initial.type == VALUE_BOOL);
                if (!matches) {
                    fail(state, statement->position,
                         "cannot initialise '%s' of type %s with a %s",
                         statement->name, expected == TYPE_INT ? "int" : "bool",
                         type_name_of(initial));
                    return false;
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
            if (condition.type != VALUE_BOOL) {
                fail(state, statement->position,
                     "condition of 'if' must be bool, got %s", type_name_of(condition));
                return false;
            }
            if (condition.as.boolean) {
                return execute_block(state, statement->then_body, statement->then_count,
                                     scope, flow);
            }
            if (statement->else_branch != NULL) {
                return execute_statement(state, statement->else_branch, scope, flow);
            }
            return true;
        }

        case STMT_WHILE: {
            while (true) {
                Value condition;
                if (!evaluate(state, statement->expression, scope, &condition)) return false;
                if (condition.type != VALUE_BOOL) {
                    fail(state, statement->position,
                         "condition of 'while' must be bool, got %s", type_name_of(condition));
                    return false;
                }
                if (!condition.as.boolean) break;

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

        case STMT_FN:
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
        if (statement->type != STMT_FN) continue;

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
            if (statement->type == STMT_FN) continue;

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
