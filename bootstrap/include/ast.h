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
    STMT_STRUCT,
    STMT_UNION,
    STMT_MATCH,
    STMT_FAIL,
    STMT_ALLOCATOR,

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
    EXPR_BUILTIN,
    EXPR_STRUCT_INIT,
    EXPR_FIELD,
    EXPR_UNION_INIT,
    EXPR_TRY,
    EXPR_CATCH,
    EXPR_ALLOCATOR_NEW
} NodeType;

/* What a type declaration defines. An Error is a union whose variants carry no
 * payload, which is why it gets its own kind rather than being a union with
 * empty payloads everywhere. */
typedef enum {
    DECL_STRUCT,
    DECL_UNION
} DeclKind;

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
    OP_GREATER_EQUAL,

    /* Bitwise. Bound as in C: shifts tighter than comparison, then &, then ^,
     * then |. Without these no kernel is possible -- page table entries, MMIO
     * registers, and status flags are all bit patterns. */
    OP_BITWISE_AND,
    OP_BITWISE_OR,
    OP_BITWISE_XOR,
    OP_SHIFT_LEFT,
    OP_SHIFT_RIGHT,
    OP_BITWISE_NOT
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
    TYPE_CHARACTER,  /* 8-bit unsigned. Bytes. */

    /* A Struct or Union name. Resolved at run time rather than parse time,
     * because a declaration may legally appear after its use. The name is
     * carried alongside the declaration. */
    TYPE_NAMED,

    /* `Result{T, E}` -- a success type paired with an error set. Only valid as
     * a function return type; a variable of this type is deliberately not
     * expressible, so a fallible call cannot be bound and forgotten. */
    TYPE_RESULT,

    /* An allocator. A handle to a region, so unlike a struct it aliases on
     * copy: two copies are the same arena, and both hand out the same bytes.
     * That exception to value semantics is deliberate and is recorded in
     * research/design/allocators.md. */
    TYPE_ALLOCATOR
} TypeKind;

const char *type_kind_name(TypeKind type);

/* Size in bytes. `Word` reports the pointer width of the build target, which
 * is the one width that is not fixed. Named types report 0; their size comes
 * from the declaration once resolved. */
size_t type_kind_size(TypeKind type);

/* True when the type can represent only non-negative values. */
bool type_kind_is_unsigned(TypeKind type);

typedef struct Expr Expr;
typedef struct Stmt Stmt;

/* One arm of a Match, or of a Catch. A variant name, the name to bind its
 * payload to (NULL if the arm does not use it), and the statements to run.
 *
 * Shared deliberately: exhaustiveness is checked the same way for a union and
 * for an error set, and one shared grammar is what keeps it one rule rather
 * than two that drift apart. Declared before Expr because Expr holds an array
 * of them for EXPR_CATCH. */
typedef struct {
    char *variant;         /* owned */
    char *binding;         /* owned, may be NULL */
    Position position;
    Stmt **body;
    size_t body_count;
} MatchArm;

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

    /* EXPR_STRUCT_INIT and EXPR_UNION_INIT: `name` is the aggregate type.
     * Struct fields travel in `arguments` as one EXPR_ASSIGN each, so the
     * field name travels with its value and order does not matter at the call
     * site. For a union, `variant` names the variant and `payload` holds its
     * value, or NULL for a payload-less variant. */
    char *variant;         /* owned */
    Expr *payload;

    /* EXPR_TRY and EXPR_CATCH use `left` for the fallible expression. */

    /* EXPR_CATCH: arms over the callee's error set, parsed exactly like Match
     * arms so there is one grammar for exhaustiveness rather than two. */
    MatchArm *arms;        /* owned */
    size_t arm_count;

    /* Set by the parser when this call is the direct operand of Try or Catch.
     * The parser is the only place that can see this relationship, and rule 1
     * -- an unhandled fallible call is an error -- needs it. */
    bool handled;
} Expr;

/* A local variable or a function parameter. */
typedef struct {
    char *name;
    TypeKind type;
    char *type_name;       /* owned; set when type is TYPE_NAMED */
    Position position;
} Parameter;

/* A named field of a struct, or a variant of a union. */
typedef struct {
    char *name;
    TypeKind type;         /* ignored for a payload-less union variant */
    Position position;
    bool has_payload;      /* union variants only */
} Field;

struct Stmt {
    NodeType type;
    Position position;

    Expr *expression;       /* STMT_EXPR, STMT_RETURN, IF/LOOP, MATCH subject */

    char *name;             /* STMT_VARIABLE, STMT_FUNCTION, STMT_STRUCT,
                             * STMT_UNION: owned */
    TypeKind declared_type; /* STMT_VARIABLE, STMT_FUNCTION */
    /* When `declared_type` is TYPE_NAMED, this holds the type's name. Owned. */
    char *type_name;
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

    /* STMT_FUNCTION, when `declared_type` is TYPE_RESULT: the success type and
     * the error set's name. Every error leaving this function passes through
     * this declaration, which is what makes rule 2 checkable. */
    TypeKind result_ok_type;
    char *result_ok_name;  /* owned; set when the success type is named */
    char *result_error_name; /* owned */

    /* STMT_STRUCT and STMT_UNION */
    DeclKind decl_kind;
    Field *fields;         /* owned array */
    size_t field_count;

    /* STMT_MATCH */
    MatchArm *arms;        /* owned array */
    size_t arm_count;
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
