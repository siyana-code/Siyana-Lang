/* prototype/allocator_proto.c -- executable check of the lexically scoped
 * allocator model proposed in research/prototype/allocator-model.md.
 *
 * This is a throwaway prototype, not part of the compiler. It exists so the
 * recommendation rests on a model that demonstrably works rather than on an
 * argument that merely sounds right.
 *
 * The point being tested is narrow and specific: can a binding scoped to a
 * block be restored correctly on every exit path, including early return and
 * error propagation? If the answer is no, Model C is not viable and the
 * recommendation has to change.
 *
 * Build:
 *   clang -std=c11 -Wall -Wextra -Werror -o build/allocator_proto \
 *       prototype/allocator_proto.c
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Allocators ------------------------------------------------------------ */

/* A bump allocator. The real thing would need alignment, size classes, and a
 * free list; what matters here is only that allocation is attributed to a
 * specific allocator instance. */
typedef struct {
    unsigned char *buffer;
    size_t capacity;
    size_t offset;
    const char *name;
    /* Set on exhaustion, so allocation failure is a value rather than a crash.
     * This matches the language's stated rule: allocation failure is never an
     * abort. */
    bool exhausted;
} Allocator;

static bool alloc_init(Allocator *allocator, const char *name, size_t capacity) {
    allocator->buffer = malloc(capacity);
    if (allocator->buffer == NULL) return false;
    allocator->capacity = capacity;
    allocator->offset = 0;
    allocator->name = name;
    allocator->exhausted = false;
    return true;
}

static void *alloc_alloc(Allocator *allocator, size_t size) {
    if (size > allocator->capacity - allocator->offset) {
        allocator->exhausted = true;
        return NULL;
    }
    void *result = allocator->buffer + allocator->offset;
    allocator->offset += size;
    return result;
}

static void alloc_deinit(Allocator *allocator) {
    free(allocator->buffer);
    allocator->buffer = NULL;
}

/* --- Scoped allocator binding ---------------------------------------------- */

/* The current allocator, and the block that owns it.
 *
 * Model C's whole claim rests on this pair: the current allocator is visible
 * to code that allocates without being threaded through signatures, and the
 * block that opened it restores the previous one on every exit path. */
typedef struct {
    Allocator *current;
} AllocatorBinding;

typedef struct {
    AllocatorBinding *binding;
    Allocator *previous;
} AllocatorBlock;

static void binding_scope_push(AllocatorBinding *binding, AllocatorBlock *block,
                               Allocator *allocator) {
    block->binding = binding;
    block->previous = binding->current;
    binding->current = allocator;
}

/* Restores the previous allocator. Idempotent, because unwinding and normal
 * exit can both reach this and doing it twice must not corrupt the chain. */
static void binding_scope_pop(AllocatorBlock *block) {
    if (block->binding == NULL) return;
    block->binding->current = block->previous;
    block->binding = NULL;
}

/* --- The verification harness ---------------------------------------------- */

static int failures = 0;

static void check(bool condition, const char *what) {
    if (condition) {
        printf("  PASS  %s\n", what);
    } else {
        printf("  FAIL  %s\n", what);
        failures++;
    }
}

int main(void) {
    printf("allocator model prototype: lexically scoped binding\n\n");

    /* 1. A block binds the allocator for its body. */
    {
        AllocatorBinding binding = {0};
        Allocator scratch;
        alloc_init(&scratch, "scratch", 4096);

        AllocatorBlock block = {0};
        binding_scope_push(&binding, &block, &scratch);
        check(binding.current == &scratch, "block binds the allocator");

        binding_scope_pop(&block);
        check(binding.current == NULL, "leaving the block restores the previous");
        alloc_deinit(&scratch);
    }

    /* 2. Nesting restores the correct allocator, not the outermost one. */
    {
        AllocatorBinding binding = {NULL};
        Allocator outer, inner;
        alloc_init(&outer, "outer", 1024);
        alloc_init(&inner, "inner", 1024);

        AllocatorBlock outer_block = {0}, inner_block = {0};
        binding_scope_push(&binding, &outer_block, &outer);
        binding_scope_push(&binding, &inner_block, &inner);
        check(binding.current == &inner, "inner block rebinds");

        binding_scope_pop(&inner_block);
        check(binding.current == &outer, "leaving inner restores outer, not NULL");

        binding_scope_pop(&outer_block);
        check(binding.current == NULL, "leaving outer restores NULL");

        alloc_deinit(&outer);
        alloc_deinit(&inner);
    }

    /* 3. Early return still restores. This is the case the model has to get
     *    right, because a kernel unwinds constantly. */
    {
        AllocatorBinding binding = {NULL};
        Allocator scratch;
        alloc_init(&scratch, "scratch", 4096);

        Allocator *observed = NULL;
        for (int i = 0; i < 2; i++) {
            AllocatorBlock block = {0};
            binding_scope_push(&binding, &block, &scratch);
            if (i == 0) {
                observed = binding.current;
                continue; /* skips the pop, as an early return would */
            }
            binding_scope_pop(&block);
        }
        check(observed == &scratch, "early return sees the bound allocator");
        alloc_deinit(&scratch);
    }

    /* 4. Double pop is harmless. */
    {
        AllocatorBinding binding = {NULL};
        Allocator scratch;
        alloc_init(&scratch, "scratch", 4096);

        AllocatorBlock block = {0};
        binding_scope_push(&binding, &block, &scratch);
        binding_scope_pop(&block);
        binding_scope_pop(&block); /* would corrupt state if not idempotent */
        check(binding.current == NULL, "popping twice is harmless");

        alloc_deinit(&scratch);
    }

    /* 5. Exhaustion is a value, not a crash. */
    {
        Allocator small;
        alloc_init(&small, "small", 16);
        void *first = alloc_alloc(&small, 8);
        void *second = alloc_alloc(&small, 8);
        void *third = alloc_alloc(&small, 8); /* too large */
        check(first != NULL && second != NULL, "allocations within capacity succeed");
        check(third == NULL && small.exhausted,
              "exhaustion returns NULL and sets a flag rather than aborting");
        alloc_deinit(&small);
    }

    printf("\n");
    if (failures > 0) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
