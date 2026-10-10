/* common.h -- shared definitions for the Siyana Lang bootstrap.
 *
 * The bootstrap is written in C11 and targets hosted x86-64 first. Everything
 * here is deliberately plain C: this code is the temporary compiler for
 * Siyana Lang, and it gets rewritten in Siyana Lang in Phase 6. Keeping it
 * small and obvious is worth more than making it fast.
 */

#ifndef SIYANA_COMMON_H
#define SIYANA_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SIYANA_VERSION_MAJOR 0
#define SIYANA_VERSION_MINOR 1
#define SIYANA_VERSION_PATCH 0
#define SIYANA_VERSION_STRING "0.1.0"

/* Phase currently under construction. Reported by `siyana --version` so it is
 * obvious at a glance which milestones are actually live. */
typedef enum {
    SIYANA_PHASE_1_SCAFFOLDING = 1,
    SIYANA_PHASE_2_INTERPRETER = 2,
    SIYANA_PHASE_3_SEMANTIC_CORE = 3,
    SIYANA_PHASE_4_FREESTANDING = 4,
    SIYANA_PHASE_5_CLI_STDLIB = 5,
    SIYANA_PHASE_6_SELF_HOSTED_FRONTEND = 6,
    SIYANA_PHASE_7_OWN_BACKEND = 7,
    SIYANA_PHASE_8_BOOTABLE_KERNEL = 8
} SiyanaPhase;

const char *siyana_version_string(void);
const char *siyana_phase_name(SiyanaPhase phase);

#endif /* SIYANA_COMMON_H */
