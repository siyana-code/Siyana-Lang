/* ast.h -- syntax tree for Siyana Lang.
 *
 * Phase 3 covers Number and Character as the only primitive types, with every
 * other type derived in the standard library. See
 * research/design/type-system.md.
 */

#ifndef SIYANA_AST_H
#define SIYANA_AST_H

#include <stdbool.h>
#include <stddef.h>

#include "token.h"

typedef enum {
    /* Statements */
    STMT_EXPR,
    STMT_VARIABLE,
    STMT_BLOCK,
    STMT_IF,
    STMT_LOOP,
    STMT_RETURN,
    STMT_FUNCTION,

    /* Expressions */
    EXPR_NUMBER,
    EXPR_CHARACTER,
    EXPR_STRING,
    EXPR_BOOL,
    EXPR_IDENT,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_ASSIGN,
    EXPR_CALL,
    EXPR_BUILTIN
} NodeType;

typedef enum {
    OP_NEGATE,
    OP_NOT,

    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,

    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_LESS,
    OP_LESS_EQUAL,
    OP_GREATER,
    OP_GREATER_EQUAL
} Operator;

/* The integer types.
 *
 * An OS cannot be written with a single unqualified integer: a network header,
 * an ioctl struct, a memory address, and a buffer size all need different
 * widths. See research/design/integer-types.md, which records the research
 * behind this set.
 *
 * bool is not a separate type. It is Number restricted to 0 and 1, following
 * C (decision 17). */
typedef enum {
    TYPE_NUMBER,     /* 64-bit signed. The default. */
    TYPE_NUMBER32,   /* 32-bit signed. Error codes and 32-bit ABI values. */
    TYPE_WORD,       /* Pointer-width unsigned. Sizes, indexes, addresses. */
    TYPE_CHARACTER   /* 8-bit unsigned. Bytes. */
} TypeKind;

const char *type_kind_name(TypeKind type);

/* Size in bytes. `Word` reports the pointer width of the build target, which
 * is the one width that is not fixed. */
size_t type_kind_size(TypeKind type);

/* True when the type can represent only non-negative values. */
bool type_kind_is_unsigned(TypeKind type);

typedef struct Expr Expr;
typedef struct Stmt Stmt;

/* A node carries both statement and expression fields because the union is
 * shared: STMT_EXPR and STMT_RETURN hold an Expr, and EXPR_CALL holds a Stmt*.
 * Separate structs would avoid the overlap but double the node count for
 * marginal benefit at this size. */

typedef struct Expr {
    NodeType type;
    Position position;

    /* EXPR_NUMBER */
    long long number;
    /* EXPR_BOOL */
    bool boolean;
    /* EXPR_CHARACTER, EXPR_STRING: owned */
    char *text;
    size_t text_length;

    /* EXPR_IDENT */
    char *name;            /* owned */

    /* EXPR_UNARY, EXPR_BINARY */
    Operator op;

    /* Children. Unused fields stay NULL. */
    Expr *left;
    Expr *right;

    /* EXPR_CALL */
    Expr *callee;
    Expr **arguments;
    size_t argument_count;

    /* EXPR_BUILTIN. A NULL callee marks Terminal.Write / Terminal.ReadLine,
     * which the parser recognises as keywords rather than identifiers. */
    TokenType builtin;
} Expr;

/* A local variable or a function parameter. */
typedef struct {
    char *name;
    TypeKind type;
    Position position;
} Parameter;

struct Stmt {
    NodeType type;
    Position position;

    Expr *expression;       /* STMT_EXPR, STMT_RETURN, IF/LOOP condition */

    char *name;             /* STMT_VARIABLE, STMT_FUNCTION: owned */
    TypeKind declared_type; /* STMT_VARIABLE, STMT_FUNCTION */
    Expr *initializer;      /* STMT_VARIABLE: may be NULL */

    /* Statement arrays.
     *
     * `body`/`body_count` serve STMT_BLOCK and STMT_FUNCTION alike: a node is
     * one or the other, never both, so they cannot collide. The IF and LOOP
     * forms use their own fields for clarity at the call site. */
    Stmt **body;
    size_t body_count;

    /* STMT_IF. Branches are stored as plain statement arrays rather than
     * wrapper Stmt nodes, because a then-branch is always exactly one block
     * and an else-branch is either a block or a nested STMT_IF. */
    Stmt **then_body;
    size_t then_count;
    Stmt *else_branch;      /* may be NULL; holds a STMT_BLOCK or STMT_IF */

    /* STMT_LOOP */
    Stmt **loop_body_statements;
    size_t loop_count;

    /* STMT_FUNCTION: the body occupies `body`/`body_count`.
     * `declares_return` distinguishes `Function f() { }` from
     * `Function f() -> Number { }`. A function without a declared return type
     * may not Return a value, which is checked at run time rather than being
     * inferred. */
    Parameter *parameters;  /* owned array */
    size_t parameter_count;
    bool declares_return;
};

/* A declared return type. A function without one may not Return a value. */
typedef struct {
    bool declared;
    TypeKind type;
} ReturnType;

typedef struct {
    Stmt **statements;
    size_t count;
} Program;

/* Frees every node reachable from the program. */
void program_free(Program *program);

#endif /* SIYANA_AST_H */
