/* driver.c -- file loading and the run pipeline.
 *
 * Kept separate from main.c so that main.c stays a command-line parser and the
 * pipeline (read -> lex -> parse -> execute) is one obvious function.
 */

#include "driver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "diagnostics.h"
#include "interp.h"
#include "lexer.h"
#include "parser.h"

/* Reads an entire file. Returns NULL and reports the reason on failure. */
static char *read_file(const char *path, size_t *out_length) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "siyana: cannot open '%s'\n", path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fprintf(stderr, "siyana: cannot seek '%s'\n", path);
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        fprintf(stderr, "siyana: cannot size '%s'\n", path);
        fclose(file);
        return NULL;
    }
    rewind(file);

    char *buffer = malloc((size_t)size + 1);
    if (buffer == NULL) {
        fprintf(stderr, "siyana: out of memory reading '%s'\n", path);
        fclose(file);
        return NULL;
    }

    size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read] = '\0';
    *out_length = read;
    return buffer;
}

int driver_run(const char *path) {
    size_t source_length = 0;
    char *source = read_file(path, &source_length);
    if (source == NULL) return 1;

    /* The filename is shortened for display, but errors report the name the
     * user typed, which is what they can act on. */
    const char *display = path;

    Lexer lexer;
    Token *tokens = lexer_scan(&lexer, source, source_length);
    if (tokens == NULL) {
        diagnostic_print(stderr, display, source, source_length,
                         lexer_error_position(&lexer), lexer_error_message(&lexer));
        free(source);
        return 1;
    }

    Program program = {0};
    Lexer parse_error_sink;
    bool parsed = parser_parse(tokens, &program, &parse_error_sink);
    token_array_free(tokens);

    if (!parsed) {
        diagnostic_print(stderr, display, source, source_length,
                         parse_error_sink.error_position, parse_error_sink.error_message);
        free(source);
        return 1;
    }

    Interp interp = {0};
    int result = interp_run(&program, &interp);
    if (interp.had_error) {
        diagnostic_print(stderr, display, source, source_length,
                         interp.error_position, interp.message);
    }

    program_free(&program);
    free(source);
    return result;
}
