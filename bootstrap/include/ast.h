/* ast.h -- syntax tree for Siyana Lang.
 *
 * Phase 2 covers the statement and expression forms listed in decision 13.
 * The tree is a plain tagged union over heap-allocated nodes: no arena yet,
 * because the language's own allocator model is a Phase 3 decision and this is
 * the C bootstrap, not Siyana Lang.
 */

#ifndef SIYANA_AST_H
#define SIYANA_AST_H

#include <stdbool.h>
#include <stddef.h>

#include "token.h"

typedef enum {
    /* Statements */
    STMT_EXPR,
    STMT_VAR,
    STMT_BLOCK,
    STMT_IF,
    STMT_WHILE,
    STMT_RETURN,
    STMT_FN,

    /* Expressions */
    EXPR_INT,
    EXPR_BOOL,
    EXPR_STRING,
    EXPR_IDENT,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_ASSIGN,
    EXPR_CALL
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

/* A node carries both statement and expression fields because the union is
 * shared: STMT_EXPR and STMT_RETURN hold an Expr, and EXPR_CALL holds a Stmt*.
 * Separate structs would avoid the overlap but double the node count for
 * marginal benefit at this size. */

typedef struct Expr Expr;
typedef struct Stmt Stmt;

typedef struct Expr {
    NodeType type;
    Position position;

    /* Literals */
    long long integer;
    bool boolean;
    char *string;          /* EXPR_STRING: owned */

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
} Expr;

/* A local variable or a function parameter. Phase 2 supports `int` and `bool`
 * only; the declared type is retained so a Phase 3 type checker has somewhere
 * to read it from rather than re-parsing. */
typedef enum {
    TYPE_INT,
    TYPE_BOOL
} TypeKind;

typedef struct {
    char *name;
    TypeKind type;
    Position position;
} Parameter;

struct Stmt {
    NodeType type;
    Position position;

    Expr *expression;       /* STMT_EXPR, STMT_RETURN, IF/WHILE condition */

    char *name;             /* STMT_VAR, STMT_FN: owned */
    TypeKind declared_type; /* STMT_VAR, STMT_FN */
    Expr *initializer;      /* STMT_VAR: may be NULL */

    /* Statement arrays.
     *
     * `body`/`body_count` serve STMT_BLOCK and STMT_FN alike: a node is one or
     * the other, never both, so they cannot collide. The IF and WHILE forms
     * use their own fields for clarity at the call site. */
    Stmt **body;
    size_t body_count;

    /* STMT_IF. Branches are stored as plain statement arrays rather than
     * wrapper Stmt nodes, because a then-branch is always exactly one block
     * and an else-branch is either a block or a nested STMT_IF. */
    Stmt **then_body;
    size_t then_count;
    Stmt *else_branch;      /* may be NULL; holds a STMT_BLOCK or STMT_IF */

    /* STMT_WHILE */
    Stmt **loop_body_statements;
    size_t loop_count;

    /* STMT_FN: the body occupies `body`/`body_count`. */
    Parameter *parameters;  /* owned array */
    size_t parameter_count;
};

typedef struct {
    Stmt **statements;
    size_t count;
} Program;

/* Frees every node reachable from the program. */
void program_free(Program *program);

#endif /* SIYANA_AST_H */
