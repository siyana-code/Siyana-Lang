/* parser.h -- recursive-descent parser for Siyana Lang. */

#ifndef SIYANA_PARSER_H
#define SIYANA_PARSER_H

#include <stdbool.h>

#include "ast.h"
#include "lexer.h"

/* Parses a token array into a Program.
 *
 * On failure returns false and fills the parser error state; on success the
 * program owns every node and must be released with program_free().
 *
 * Syntax errors are reported at the first problem and parsing stops there.
 * Recovering from errors is deliberately not attempted at this phase: a single
 * clear error is worth more than a cascade of confusing ones.
 */
bool parser_parse(const Token *tokens, Program *out_program,
                  Lexer *error_sink);

#endif /* SIYANA_PARSER_H */
