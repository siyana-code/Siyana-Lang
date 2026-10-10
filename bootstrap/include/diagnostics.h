/* diagnostics.h -- error reporting.
 *
 * Diagnostics carry a source position and print with a caret pointing at the
 * offending token. Phase 2's exit criteria require that parse failures report
 * line and column and that runtime type errors name the offending expression,
 * so this is deliberately a first-class component rather than ad-hoc printing.
 */

#ifndef SIYANA_DIAGNOSTICS_H
#define SIYANA_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdio.h>

#include "token.h"

/* Prints:
 *
 *   hello.sy:3:14: error: expected ';', found identifier
 *     |
 *   3 |     print(1 +)
 *     |              ^
 */
void diagnostic_print(FILE *stream, const char *filename, const char *source,
                      size_t source_length, Position position, const char *message);

#endif /* SIYANA_DIAGNOSTICS_H */
