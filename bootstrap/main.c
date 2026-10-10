/* main.c -- entry point for the Siyana Lang bootstrap compiler.
 *
 * Phase 1 scope: the binary builds, runs, and reports its version. No language
 * features are implemented yet -- see docs/architecture.md for the phase plan.
 */

#include "common.h"

#include <stdio.h>
#include <string.h>

static void print_usage(const char *program) {
    fprintf(stderr, "usage: %s [options] [file.sy]\n", program);
    fprintf(stderr, "\n");
    fprintf(stderr, "Siyana Lang %s -- bootstrap compiler\n", siyana_version_string());
    fprintf(stderr, "\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  --version     print version and exit\n");
    fprintf(stderr, "  --help        print this message and exit\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "No language features are implemented yet. This binary exists to\n");
    fprintf(stderr, "establish the build and the command-line interface.\n");
}

int main(int argc, char **argv) {
    const char *program = (argc > 0 && argv[0] != NULL) ? argv[0] : "siyana";

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--version") == 0) {
            printf("siyana %s\n", siyana_version_string());
            printf("phase: %s\n", siyana_phase_name(SIYANA_PHASE_1_SCAFFOLDING));
            return 0;
        }

        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            print_usage(program);
            return 0;
        }

        /* The first non-flag argument is a source file. Nothing can be done
         * with it yet, so say so plainly rather than silently ignoring it. */
        if (arg[0] != '-') {
            fprintf(stderr, "siyana: cannot compile '%s' yet\n", arg);
            fprintf(stderr, "siyana: the interpreter is not implemented (Phase 1 of 8)\n");
            return 1;
        }

        fprintf(stderr, "siyana: unknown option '%s'\n", arg);
        fprintf(stderr, "siyana: try '%s --help'\n", program);
        return 1;
    }

    /* No arguments: report what this is rather than reading stdin, which would
     * hang in a terminal and looks like a crash when run from a script. */
    printf("siyana %s\n", siyana_version_string());
    printf("phase: %s\n", siyana_phase_name(SIYANA_PHASE_1_SCAFFOLDING));
    printf("\nNo source file given. Run '%s --help' for usage.\n", program);
    return 0;
}
