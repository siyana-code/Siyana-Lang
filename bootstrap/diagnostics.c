/* diagnostics.c -- error reporting with source context. */

#include "diagnostics.h"

#include <stdlib.h>
#include <string.h>

/* Finds the byte range of the line containing `line` (1-based). Returns false
 * if the line is out of range. */
static bool find_line(const char *source, size_t source_length, int line,
                      size_t *out_start, size_t *out_end) {
    if (line < 1) return false;

    size_t current_line = 1;
    size_t start = 0;

    for (size_t i = 0; i <= source_length; i++) {
        if (current_line == (size_t)line) {
            size_t end = start;
            while (end < source_length && source[end] != '\n') end++;
            *out_start = start;
            *out_end = end;
            return true;
        }
        if (i == source_length) break;
        if (source[i] == '\n') {
            current_line++;
            start = i + 1;
        }
    }
    return false;
}

void diagnostic_print(FILE *stream, const char *filename, const char *source,
                      size_t source_length, Position position, const char *message) {
    fprintf(stream, "%s:%d:%d: error: %s\n", filename, position.line, position.column, message);

    size_t line_start = 0;
    size_t line_end = 0;
    if (!find_line(source, source_length, position.line, &line_start, &line_end)) {
        return; /* position is past the end of the source; nothing to show */
    }

    size_t line_number_width = 4;
    for (int n = position.line; n >= 100; n /= 10) line_number_width++;

    fprintf(stream, "%*s |\n", (int)line_number_width, "");
    fprintf(stream, "%d | %.*s\n", position.line, (int)(line_end - line_start),
            source + line_start);
    fprintf(stream, "%*s | ", (int)line_number_width, "");

    /* Column is 1-based in the AST, 0-based as an indent. */
    for (int i = 1; i < position.column; i++) fputc(' ', stream);
    fputs("^\n", stream);
}
