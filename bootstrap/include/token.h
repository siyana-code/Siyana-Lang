/* token.h -- lexical tokens for Siyana Lang.
 *
 * Keywords are spelled out in full (decision 16): `If`, `Loop`, `Function`.
 * They cannot collide with user identifiers, and `grep Loop` finds every loop.
 */

#ifndef SIYANA_TOKEN_H
#define SIYANA_TOKEN_H

/* Self-contained: a header should compile on its own, so it pulls in what it
 * uses rather than relying on includers to have done it. */
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TOKEN_EOF,
    TOKEN_LEX_ERROR,   // lexer failure marker; not a keyword

    /* Literals */
    TOKEN_IDENT,
    TOKEN_NUMBER,
    TOKEN_CHARACTER,
    TOKEN_STRING,

    /* Types */
    TOKEN_NUMBER_TYPE,
    TOKEN_NUMBER32_TYPE,
    TOKEN_WORD_TYPE,
    TOKEN_CHARACTER_TYPE,

    /* Declarations */
    TOKEN_VARIABLE,
    TOKEN_FUNCTION,

    /* Control flow */
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_LOOP,
    TOKEN_RETURN,

    /* Boolean literals, derived from Number (decision 17) */
    TOKEN_TRUE,
    TOKEN_FALSE,

    /* Builtins. Terminal is a value, not a module, so that it can later be
     * passed and stored like any other object. */
    TOKEN_TERMINAL,
    TOKEN_WRITE,
    TOKEN_READ_LINE,

    /* Aggregate declarations */
    TOKEN_STRUCT,
    TOKEN_UNION,
    TOKEN_ERROR,      /* `Error Name { ... }` -- a payload-less union */

    /* Match */
    TOKEN_MATCH,

    /* Errors. `Try` and `Catch` are postfix operators on an expression:
     * `Open(path) Try` propagates, `Open(path) Catch { ... }` handles. */
    TOKEN_RESULT,
    TOKEN_TRY,
    TOKEN_CATCH,
    TOKEN_FAIL,

    /* Allocators. A keyword because `Allocator(4096)` is a constructor, and a
     * constructor spelled like a call needs the type name reserved. */
    TOKEN_ALLOCATOR,

    /* Punctuation */
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_COLON,
    TOKEN_DOT,

    /* Operators */
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_ASSIGN,
    TOKEN_EQ,
    TOKEN_BANG,
    TOKEN_BANG_EQ,
    TOKEN_LT,
    TOKEN_LT_EQ,
    TOKEN_GT,
    TOKEN_GT_EQ,

    /* Bitwise. `<<` and `>>` are above the relational operators, as in C,
     * because a shift binds tighter than a comparison and `x << 3 == y`
     * should mean what it reads as. */
    TOKEN_AMPERSAND,
    TOKEN_PIPE,
    TOKEN_CARET,
    TOKEN_TILDE,
    TOKEN_SHIFT_LEFT,
    TOKEN_SHIFT_RIGHT,

    /* Indexing and array types. */
    TOKEN_LBRACKET,
    TOKEN_RBRACKET
} TokenType;

/* A source position. Both are 1-based so diagnostics can be printed directly
 * without adjustment at print time. */
typedef struct {
    int line;
    int column;
} Position;

typedef struct {
    TokenType type;
    Position start;
    /* Byte offset into the source. A token's text is source[start_offset ..
     * start_offset + length). */
    size_t start_offset;
    size_t length;
    /* Owned by the lexer; valid for the life of the token array. Set for
     * TOKEN_IDENT, TOKEN_NUMBER, TOKEN_CHARACTER, and TOKEN_STRING. */
    char *text;
} Token;

const char *token_type_name(TokenType type);
/* Human-readable description used in diagnostics, e.g. "';'" or "identifier". */
const char *token_type_description(TokenType type);

#endif /* SIYANA_TOKEN_H */
