/* token.h -- lexical tokens for Siyana Lang.
 *
 * Phase 2 slice: integers, bool, string literals, identifiers, the operators
 * needed for arithmetic and comparison, and the keywords for the statement
 * forms. See docs/architecture.md for the full feature set.
 */

#ifndef SIYANA_TOKEN_H
#define SIYANA_TOKEN_H

/* Self-contained: a header should compile on its own, so it pulls in what it
 * uses rather than relying on includers to have done it. */
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TOKEN_EOF,
    TOKEN_ERROR,

    /* Literals */
    TOKEN_IDENT,
    TOKEN_INT,
    TOKEN_STRING,

    /* Keywords */
    TOKEN_FN,
    TOKEN_VAR,
    TOKEN_RETURN,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_BOOL,
    TOKEN_INT_TYPE,

    /* Builtins.
     *
     * Builtins are keywords rather than ordinary identifiers so a call to one
     * is visible in the token stream and can be diagnosed as such. Phase 5
     * replaces this with a proper namespace, and the grammar changes then. */
    TOKEN_PRINT,

    /* Punctuation */
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_COLON,

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
    TOKEN_GT_EQ
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
    /* Owned by the lexer; valid for the life of the token array. NULL for
     * tokens with no literal payload. */
    char *text;
} Token;

const char *token_type_name(TokenType type);
/* Human-readable description used in diagnostics, e.g. "';'" or "identifier". */
const char *token_type_description(TokenType type);

#endif /* SIYANA_TOKEN_H */
