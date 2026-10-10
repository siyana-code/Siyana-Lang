/* lexer.h -- tokenizer for Siyana Lang. */

#ifndef SIYANA_LEXER_H
#define SIYANA_LEXER_H

#include <stdbool.h>
#include <stddef.h>

#include "token.h"

typedef struct {
    const char *source;
    size_t source_length;
    size_t offset;
    Position position;
    /* Set when a token fails to lex. The lexer stops at the first error and
     * reports it rather than continuing, so a cascade of confusing secondary
     * errors is avoided. */
    bool had_error;
    char error_message[128];
    Position error_position;
} Lexer;

/* Allocate a token array from source text. Always returns NULL on allocation
 * failure; check lexer_error() to distinguish that from a lexing error. */
Token *lexer_scan(Lexer *lexer, const char *source, size_t length);

bool lexer_had_error(const Lexer *lexer);
const char *lexer_error_message(const Lexer *lexer);
Position lexer_error_position(const Lexer *lexer);

void token_array_free(Token *tokens);

#endif /* SIYANA_LEXER_H */
