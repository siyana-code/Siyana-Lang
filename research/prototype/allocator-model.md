# Allocator Model — Prototypes

The highest-risk decision in the plan. This compares the three candidate
models against the *same* program so the trade-off is concrete rather than
theoretical.

The test program is deliberately awkward in a useful way: it has a function
that allocates and returns, a function that allocates but doesn't return the
memory, and a function that legitimately wants a different allocator than its
caller. Those three shapes are where the models diverge.

## The test program

```siyana
// Build a buffer of n ints, then report its sum.
fn make_buffer(alloc, n: int) {
    return alloc.alloc_raw(1024);
}

fn fill(alloc, buf, count: int) {
    var i: int = 0;
    while (i < count) {
        buf.store_int(i * 4, i);   // writes into the buffer
        i = i + 1;
    }
}

fn sum(buf, count: int) -> int {
    var total: int = 0;
    var i: int = 0;
    while (i < count) {
        total = total + buf.load_int(i * 4);
        i = i + 1;
    }
    return total;
}

fn main() -> int {
    var scratch = Arena.init(4096);        // temporary, freed on scope exit
    defer scratch.deinit();

    var buf = make_buffer(scratch.allocator(), 64);
    fill(scratch.allocator(), buf, 64);
    print(sum(buf, 64));                   // expect 2016

    var permanent = Heap.init();            // outlives main
    var kept = make_buffer(permanent.allocator(), 128);
    print(sum(kept, 128));                  // expect 8128
    return 0;
}
```

## Model A — Threaded (Zig-style)

Every function that allocates takes an allocator parameter and passes it
along.

```siyana
fn make_buffer(alloc: Allocator, n: int) {
    return alloc.alloc(n * 4);
}

fn fill(alloc: Allocator, buf: Ptr, count: int) {
    var i: int = 0;
    while (i < count) {
        buf.write_int(i * 4, i);
        i = i + 1;
    }
}

fn sum(buf: Ptr, count: int) -> int { ... }

fn main() -> int {
    var scratch = Arena.init(4096);
    defer scratch.deinit();

    var buf = make_buffer(scratch.allocator(), 64);
    fill(scratch.allocator(), buf, 64);     // <- allocator passed again
    print(sum(buf, 64));

    var permanent = Heap.init();
    var kept = make_buffer(permanent.allocator(), 128);
    print(sum(kept, 128));
    return 0;
}
```

**Reading `fill`:** the allocator is there, so you can see that filling a
buffer allocates. You can delete the parameter and the compiler tells you
which call sites break — the type system enforces honesty.

**Cost:** `fill` never returns memory and never frees anything. The allocator
parameter is pure noise. In a real codebase this parameter appears on every
function that transitively allocates, and readers must see past it to find the
actual work.

**Scale of the noise.** In the test program: 1 of 4 functions carries a
meaningless allocator. In a deep call chain it is worse — a function 6 levels
down may allocate on behalf of its caller 6 levels up.

## Model B — Ambient (Odin-style)

The allocator is reachable implicitly from the current call, and can be
swapped globally without changing signatures.

```siyana
fn make_buffer(n: int) {
    return alloc.alloc(n * 4);      // which allocator? whichever is current
}

fn fill(buf: Ptr, count: int) { ... }

fn main() -> int {
    var scratch = Arena.init(4096);
    push_allocator(scratch.allocator());   // global or thread-local state
    defer pop_allocator();

    var buf = make_buffer(64);
    fill(buf, 64);
    print(sum(buf, 64));
    ...
}
```

**Reading `fill`:** clean. No plumbing. Every function that allocates is
honest about *that* it allocates, which is most of the value of explicit
allocation.

**Cost:** hidden state. `make_buffer` does not say where the memory comes
from. Two calls to `make_buffer` in different places can use different
allocators with no visible difference. In a kernel this is the dangerous
case: allocating from a temporary arena in a function you believed was
long-lived.

**Verdict:** this directly contradicts the project's own no-hidden-state
principle. It buys readability at the cost of the property the language is
meant to guarantee.

## Model C — Lexically scoped

The allocator is bound for a block and restored afterwards. Allocation stays
visible at the point of decision; plumbing disappears from everything below.

```siyana
fn make_buffer(alloc: Allocator, n: int) {
    return alloc.alloc(n * 4);
}

fn fill(buf: Ptr, count: int) { ... }       // no allocator: allocates nothing

fn main() -> int {
    var permanent = Heap.init();
    var kept = make_buffer(permanent.allocator(), 128);

    allocator(scratch.allocator()) {         // <- bound for this block
        var buf = make_buffer(scratch.allocator(), 64);
        fill(buf, 64);
        print(sum(buf, 64));
    }                                       // <- restored here

    print(sum(kept, 128));
    return 0;
}
```

**Reading `fill`:** clean, because `fill` genuinely does not allocate. The
allocator appears only on functions that do. That is the property Model A
loses and Model C keeps.

**Reading `make_buffer`:** unchanged from Model A. Allocation is visible where
it happens.

**The block form.** `allocator(x) { ... }` binds `x` for the block and restores
the previous allocator on exit, including on early return or error propagation.
This is what makes the scratch allocator safe: nothing inside the block can
accidentally allocate from `permanent`.

**Cost:** a new block form, and the compiler must restore the binding on every
exit path. That is real work in Phase 3 and a real source of bugs — but it is
contained, and it is the kind of bug a test suite can find.

## Comparison

| | A: Threaded | B: Ambient | C: Lexically scoped |
| --- | --- | --- | --- |
| Allocation visible at call site | Yes | No | Yes |
| Non-allocating functions stay clean | No | Yes | Yes |
| Compiler enforces which allocator | Yes | No | Yes |
| Matches no-hidden-state principle | Yes | No | Yes |
| Swapping allocator per call | Yes | Yes | Yes |
| Cost | Plumbing | Hidden state | New block form |

## Recommendation

**Model C — lexically scoped.**

It is the only one of the three that keeps both properties the project needs:
allocation is visible where it happens, *and* functions that do not allocate do
not carry an allocator parameter. Model A is honest but noisy. Model B is
clean but breaks the design principle the language exists to uphold.

Model C also composes with the OS target better than it first appears: an OS
needs per-subsystem allocators (page allocator, slab, per-task arena), and
lexical binding expresses "this region of code allocates from here" in a way
that survives refactoring.

## Risks

**The block form is not free.** Restoring the binding on every exit path —
including error propagation, which does not exist yet — is where bugs will
live. Phase 3 should implement it last, after error handling, so there is one
mechanism for unwinding rather than two.

**It is a novel design.** No existing language does this. The upside is
freedom; the downside is no borrowed battle-testing, so it will need more
testing than a conventional design would.

**[Unresolved]** How does this interact with error unions? If `make_buffer`
can fail, and an error unwinds through an `allocator` block, the block must
restore the binding on that path too. This cannot be designed in isolation from
the error model, which is why both belong in Phase 3 together.

**[Unresolved]** Does `Arena` get automatic deinit on scope exit, or always
explicit `defer`? Automatic is safer and matches the block form; explicit is
more predictable and matches "no hidden control flow". This is a genuine
tension that should be decided explicitly rather than defaulted.