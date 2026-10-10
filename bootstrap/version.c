/* version.c -- version and phase reporting.
 *
 * Kept separate from main.c so that when the bootstrap is rewritten in
 * Siyana Lang in Phase 6, the string literals land in an obvious place.
 */

#include "common.h"

const char *siyana_version_string(void) {
    return SIYANA_VERSION_STRING;
}

const char *siyana_phase_name(SiyanaPhase phase) {
    switch (phase) {
        case SIYANA_PHASE_1_SCAFFOLDING:
            return "1 - scaffolding";
        case SIYANA_PHASE_2_INTERPRETER:
            return "2 - interpreter core";
        case SIYANA_PHASE_3_SEMANTIC_CORE:
            return "3 - semantic core";
        case SIYANA_PHASE_4_FREESTANDING:
            return "4 - freestanding";
        case SIYANA_PHASE_5_CLI_STDLIB:
            return "5 - cli stdlib";
        case SIYANA_PHASE_6_SELF_HOSTED_FRONTEND:
            return "6 - self-hosted frontend";
        case SIYANA_PHASE_7_OWN_BACKEND:
            return "7 - own backend";
        case SIYANA_PHASE_8_BOOTABLE_KERNEL:
            return "8 - bootable kernel";
        default:
            return "unknown";
    }
}
