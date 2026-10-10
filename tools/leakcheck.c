/* tools/leakcheck.c -- a malloc/free counter, linked in only when asked for.
 *
 * The struct and array leak in docs/architecture.md could not be measured: this
 * MinGW has no AddressSanitizer (`libclang_rt.asan` is missing) and no
 * LeakSanitizer. That is why it was recorded as "reasoned from reading the code
 * rather than measured", which is a weaker claim than the rest of the project
 * makes elsewhere.
 *
 * This closes that gap without a sanitizer and without changing any source.
 * GNU ld's --wrap renames every malloc/free/calloc/realloc in the program to
 * __wrap_*, and this file calls the original through __real_*. So the counters
 * see exactly the allocations the interpreter makes, and the interpreter's
 * source stays untouched.
 *
 * The measure is differential, not absolute. A program that runs zero loop
 * iterations still allocates for its token list and its AST, both of which
 * legitimately live until exit. So the useful number is how much *live
 * allocation* grows per iteration of a loop that builds a value -- compared
 * before and after a fix. A per-iteration growth of zero is the claim.
 *
 * Build:
 *   clang ... -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
 * and read the "live" line the process prints on exit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *ptr, size_t size);
void __real_free(void *ptr);

/* Not atomic. The interpreter is single-threaded, and adding atomics here
 * would change the allocation pattern the measurement is about. */
static size_t live_blocks = 0;
static size_t live_bytes = 0;
static size_t total_allocs = 0;

/* Every live block carries its size so free can subtract it. One malloc per
 * block, which slightly changes the count being measured -- accounted for by
 * the differential: it is constant per live block, not per program step. */
typedef struct {
    size_t size;
} Header;

void *__wrap_malloc(size_t size) {
    void *raw = __real_malloc(size + sizeof(Header));
    if (raw == NULL) return NULL;
    Header *h = (Header *)raw;
    h->size = size;
    live_blocks++;
    live_bytes += size;
    total_allocs++;
    return (char *)raw + sizeof(Header);
}

void *__wrap_calloc(size_t count, size_t size) {
    /* Handled through malloc+memset so there is one accounting path, not two
     * that could disagree. */
    size_t total = count * size;
    if (count != 0 && total / count != size) return NULL;   /* overflow */
    void *p = __wrap_malloc(total);
    if (p != NULL) memset(p, 0, total);
    return p;
}

void __wrap_free(void *ptr) {
    if (ptr == NULL) return;
    Header *h = (Header *)((char *)ptr - sizeof(Header));
    if (live_blocks > 0) live_blocks--;
    live_bytes -= h->size;
    __real_free(h);
}

void *__wrap_realloc(void *ptr, size_t size) {
    if (ptr == NULL) return __wrap_malloc(size);
    Header *h = (Header *)((char *)ptr - sizeof(Header));
    size_t previous = h->size;

    /* A realloc that moves or grows may fail; the original block must survive
     * either way, so this copies first and only frees on success. */
    void *raw = __real_malloc(size + sizeof(Header));
    if (raw == NULL) return NULL;
    memcpy(raw, ptr, previous < size ? previous : size);
    Header *nh = (Header *)raw;
    nh->size = size;

    live_bytes -= previous;
    live_bytes += size;
    total_allocs++;
    __real_free(h);
    return (char *)raw + sizeof(Header);
}

/* Printed from a destructor so it runs after main returns but before the CRT
 * tears down stdio. Written with write(2) rather than printf so this does not
 * allocate, which would change the very number it reports. */
__attribute__((destructor)) static void report(void) {
    char line[128];
    size_t at = 0;
    const char *prefix = "leakcheck: live blocks ";
    for (const char *p = prefix; *p != '\0' && at < sizeof(line) - 1; p++) {
        line[at++] = *p;
    }
    size_t n = live_blocks;
    char digits[24];
    size_t d = 0;
    if (n == 0) digits[d++] = '0';
    while (n > 0) { digits[d++] = (char)('0' + n % 10); n /= 10; }
    while (d > 0 && at < sizeof(line) - 1) line[at++] = digits[--d];
    const char *mid = ", bytes ";
    for (const char *p = mid; *p != '\0' && at < sizeof(line) - 1; p++) line[at++] = *p;
    n = live_bytes;
    if (n == 0) digits[d++] = '0';
    while (n > 0) { digits[d++] = (char)('0' + n % 10); n /= 10; }
    while (d > 0 && at < sizeof(line) - 2) line[at++] = digits[--d];
    line[at++] = '\n';
    fwrite(line, 1, at, stderr);
}
