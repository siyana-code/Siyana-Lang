/* main.c -- entry point for the Siyana Lang bootstrap compiler.
 *
 * Phase 2 scope: the interpreter is implemented for the language slice in
 * decision 13. See docs/architecture.md for the phase plan.
 */

#include "common.h"
#include "driver.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const char *program) {
    fprintf(stderr, "usage: %s [options] <command> [file.sy]\n", program);
    fprintf(stderr, "\n");
    fprintf(stderr, "Siyana Lang %s\n", siyana_version_string());
    fprintf(stderr, "\n");
    fprintf(stderr, "commands:\n");
    fprintf(stderr, "  run <file.sy>   execute a Siyana Lang source file\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  --version       print version and exit\n");
    fprintf(stderr, "  --help          print this message and exit\n");
}

int main(int argc, char **argv) {
    const char *program = (argc > 0 && argv[0] != NULL) ? argv[0] : "siyana";

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--version") == 0) {
            printf("siyana %s\n", siyana_version_string());
            printf("phase: %s\n", siyana_phase_name(SIYANA_PHASE_2_INTERPRETER));
            return 0;
        }

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(program);
            return 0;
        }

        if (arg[0] == '-') {
            fprintf(stderr, "siyana: unknown option '%s'\n", arg);
            fprintf(stderr, "siyana: try '%s --help'\n", program);
            return 1;
        }

        /* First non-flag argument is the command. */
        if (strcmp(arg, "run") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "siyana: 'run' needs a source file\n");
                fprintf(stderr, "siyana: try '%s run hello.sy'\n", program);
                return 1;
            }
            return driver_run(argv[i + 1]);
        }

        fprintf(stderr, "siyana: unknown command '%s'\n", arg);
        fprintf(stderr, "siyana: try '%s --help'\n", program);
        return 1;
    }

    /* No arguments: report what this is rather than reading stdin, which would
     * hang in a terminal and looks like a crash when run from a script. */
    printf("siyana %s\n", siyana_version_string());
    printf("phase: %s\n", siyana_phase_name(SIYANA_PHASE_2_INTERPRETER));
    printf("\nNo command given. Run '%s --help' for usage.\n", program);
    return 0;
}
