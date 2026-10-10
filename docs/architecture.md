# Siyana Lang — Compiler Architecture and Plan

Status: design document. Nothing here is implemented.

This document turns the project's constraints into a build sequence with
verifiable exit criteria. It follows from the decisions recorded in
[research/](../research/), and it deliberately plans for a solo maintainer.

## Settled constraints

Everything below is designed around these. If one changes, revisit this plan.

| Constraint | Value | Consequence |
| --- | --- | --- |
| Memory model | Explicit allocator (Zig-style) | Allocator must be a first-class value from day one |
| Bootstrap | Interpreter first | Semantics are proven before any codegen work |
| Codegen | Own backend, eventually | No LLVM dependency at any point in the long term |
| Target | Bootable OS, own toolchain | Freestanding subset and ELF knowledge needed early |
| Audience | Solo, no stability obligations | Every phase must be small enough to finish alone |

## The governing principle: always leave something runnable

Every phase ends in an artifact that runs. No phase consists of "working on
the compiler" without a demonstrable output.

This is not motivational advice; it is a scope-control mechanism. The primary
failure mode of a solo language project is spending eighteen months on
infrastructure and never reaching the point where the language does anything,
then losing interest. A runnable artifact after every phase keeps the work
honest and keeps the motivation intact.

## The scoping principle: define the language by what the OS needs

The dangerous state is not "the compiler is hard." It is "the language is 90%
done," because 90% of a language is unbounded — every language author can name
three more features that ought to be added before 1.0, and none of them are
wrong in isolation.

The defence is to declare a **kernel-ready feature set**: the specific
features required to write an OS in Siyana Lang. The language is done when
that set is complete, regardless of what else could be added.

Initial kernel-ready set:

- Integers of fixed widths, `bool`, `f32`, `f64`
- Pointers, raw address arithmetic, `void`
- Fixed-size arrays, slices, `[]const T`
- Structs, enums, tagged unions
- `Maybe<T>` (optional), error unions
- Functions, first-class function pointers
- Modules with explicit imports
- Generics (monomorphised)
- Explicit allocators: at minimum bump and free-list
- Compile-time evaluation for constants and array sizing
- `asm` escape hatch, or inline intrinsics
- Bit operations, shifts, casts

Explicitly **not** in the initial set, and each of these is a real cost to
defer:

- Closures / anonymous functions
- Operator overloading beyond builtins
- Async, threads, or any runtime scheduler
- Generators, coroutines
- Reflection
- A garbage collector
- Exception unwinding

Note what is missing and why it matters: there is no GC, because an OS kernel
cannot afford a tracing collector in the general case; there is no async,
because a kernel is not an event loop.

## Phase plan

Effort is expressed in bands rather than calendar time. Calendar estimates for
a solo project on this kind of work are usually both wrong and demoralising,
and committing to them in a document is how projects die quietly.

| Band | Rough meaning |
| --- | --- |
| S | A few weeks of evenings |
| M | A few months of evenings |
| L | A year or more of evenings |

### Phase 1 — Host and repository scaffolding (S)

Decide the host language for the bootstrap interpreter and set up the repo.

**Exit criteria**

- Repository builds from a clean clone with one documented command.
- `siyana` binary runs and reports its version.
- The decision about the host language is written down, with its reasoning.

### Phase 2 — Interpreter core (M)

Lexer, parser, tree-walking evaluator. Narrow language: integers, `bool`,
arithmetic, comparison, `if`, `while`, functions, variables.

The point is to prove the pipeline end to end on a language small enough that
no design argument is needed. Resist adding features here.

**Exit criteria**

- `siyana run hello.sy` prints output.
- A failing parse produces a diagnostic with a line and column.
- A runtime type error produces a diagnostic naming the offending expression.

### Phase 3 — Semantic core (L)

The real language. Types, tagged unions, error handling, and — critically —
the explicit allocator model.

The allocator decision from Phase 2 is where this project most easily goes
wrong, so it is designed rather than improvised. See "Allocator model" below.

**Exit criteria**

- A tagged union with a payload compiles and runs.
- An error propagates from a deep call stack with its context intact.
- An allocation failure is surfaced as a value, never a crash.
- A test suite exists that is expressive enough to write the OS in.

### Phase 4 — Freestanding subset (M)

Prove the language can run with no operating system underneath it. This is the
first real test of the design claims, and it will find things that the
hosted tests cannot.

**Exit criteria**

- A program compiled by the interpreter runs on bare metal or under QEMU.
- No hosted-only feature is reachable from the freestanding build.

### Phase 5 — CLI-capable standard library (M)

The first declared milestone. Files, arguments, strings, formatted output.

**Exit criteria**

- A real CLI tool — argument parsing, file reading, error messages to stderr —
  runs in Siyana Lang.

### Phase 6 — Self-hosted frontend (L)

Move the compiler's frontend into Siyana Lang. The backend stays out of scope
for this phase.

At this point the language is unambiguously proven sufficient to build a large,
performance-sensitive systems program: the compiler.

**Exit criteria**

- `stage1` and `stage2` compilers agree byte-for-byte.
- The interpreter can be built from Siyana Lang source.

### Phase 7 — Own backend (L)

Instruction selection, register allocation, then an assembler and a linker.

Recommended order, cheapest and most valuable first:

1. x86-64 with a **naive backend**: every value lives in memory. No register
   allocation. Produces correct, slow code. If the OS boots, the design is
   validated.
2. A **stack-machine intermediate representation**, then simple peephole
   optimisation. Still no register allocation.
3. **Linear-scan register allocation.** Materially simpler than graph
   colouring, produces meaningfully better code, and is entirely adequate for
   kernel work.
4. Graph colouring (Chaitin/Briggs) — optional, and only if measurements
   justify it.

Each of steps 1–3 is independently useful. Do not skip to step 3.

**Exit criteria**

- The interpreter, compiled by the backend, passes its own test suite.
- A linker in Siyana Lang produces a valid ELF executable.

### Phase 8 — Bootable kernel (L)

The original goal.

**Exit criteria**

- A kernel boots under QEMU via UEFI and prints to the framebuffer.
- The kernel is written in Siyana Lang, using the freestanding subset.

## The bootstrap question, stated honestly

Something must compile Siyana Lang before Siyana Lang can compile itself. This
is not a design choice; it is the boundary condition every compiler project
starts from. The research on this is in
[bootstrap.md](../research/language/bootstrap.md).

Two viable paths, and the difference matters:

**Path A — interpreter in a host language, ported later.** The Phase 2–5
interpreter is written in the host language. Once the language stabilises, it
is rewritten in Siyana Lang. Cost: the interpreter is written roughly twice.

**Path B — minimal bootstrap compiler, everything in Siyana Lang.** Write a
small compiler in the host language that handles only the subset needed to
compile the real compiler. Everything after that is Siyana Lang.

**[Recommendation]** Path A, with the port treated as a deliberate milestone
rather than an afterthought. It is simpler, and the doubling is real but
bounded — the interpreter is small relative to the compiler. Zig accepts the
same cost permanently, which suggests it is tolerable.

**[Unresolved]** The host language itself has not been chosen. C is the
default candidate: it is available on every system that could plausibly run
the build, and it is honest about what the project is. This decision is not
recorded in the research folder yet and should be.

## The allocator model

This is the design decision most likely to determine whether the language is
pleasant or merely correct, so it gets its own treatment.

The constraint is fixed: explicit allocators, no hidden allocation. The open
question is how they are *passed*. Two shapes are known to work, per the
landscape research:

- **Threaded** — the allocator is a parameter, visible at every call site.
  Completely honest; the cost is plumbing through every function that
  allocates, and that plumbing is noise the reader must see past.
- **Ambient** — the allocator is reachable implicitly from the current call.
  Clean call sites; the cost is hidden state, which contradicts the project's
  own no-hidden-state principle.

A third possibility is worth considering, since no existing language gets it
right: **lexically scoped allocators**, where an allocator is bound for a
block and restored afterwards.

```
fn process(alloc: Allocator) !void {
    var scratch = Arena.init(alloc);   // explicit, visible
    defer scratch.deinit();             // freed here, deterministically

    parse(scratch.allocator());         // no plumbing to read past
}
```

This keeps allocation visible at the point of decision while removing the
plumbing from the signatures that follow. It is a genuine gap in the existing
landscape rather than a novelty, and it is worth prototyping before committing.

## Risks

Stated plainly, because the plan is only useful if the failure modes are known.

**Register allocation is the long pole.** Phase 7 dominates the schedule.
Linear scan mitigates it. Skipping to graph colouring does not help and will
cost a year.

**The project stalls at "almost done."** Mitigated by the kernel-ready feature
set and by requiring every phase to end runnable.

**The allocator model is wrong and discovered late.** Mitigated by prototyping
it in Phase 3 rather than deferring it.

**The backend becomes the project.** Code generation is endlessly expandable and
individually satisfying. Phase 7 step 1 — slow, memory-only codegen — exists
specifically so that a booting kernel is reachable without it.

**Nobody uses it, including the author.** Not a technical risk, but the real
one. The declared first milestone in Phase 5 is a CLI tool, chosen partly
because it is fast enough to reach while the motivation is still intact.

## Open questions

Not yet decided, and each blocks work:

1. Host language for the bootstrap interpreter.
2. Error handling detail: error unions versus an explicit result type.
3. Compile-time evaluation design, and how much of it the language exposes.
4. Whether closures are genuinely deferrable, or whether first-class function
   pointers suffice.
5. Module system: file-based, package-based, or both.
6. Source file extension — `.sy` is a placeholder.