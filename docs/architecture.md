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

### Phase 1 — Host and repository scaffolding (S) — COMPLETE

Set up the repository structure, the build, and the `siyana` entry point. The
host language decision is already made (C, see decision 8), so this phase is
mechanical.

**Verified, not assumed.** All four exit criteria were checked against a fresh
`git clone` of the remote rather than the working tree:

- Builds from a clean clone with one documented command — confirmed.
- `siyana --version` prints a version and exits zero — confirmed.
- Works from a clean checkout with only a C compiler — confirmed under both
  Clang 16 and GCC 13.2, debug and release.
- Host language decision recorded — done in decision 8.

Beyond the criteria, the test suite was checked for the failure direction as
well as the passing one: pointed at a deliberately non-conforming binary, it
reports 5 of 6 failures and exits non-zero. A suite that cannot fail proves
nothing.

Proposed layout:

```
bootstrap/       C sources for the interpreter (through Phase 5)
bootstrap/include/
src/             Siyana Lang sources (empty until Phase 6)
lib/             Siyana Lang standard library (empty until Phase 3)
tests/           test suite
tools/           build scripts
docs/            this plan and future design docs
research/        background material and decisions
```

**Exit criteria**

- Repository builds from a clean clone with one documented command.
- `siyana --version` prints a version and exits zero.
- The build works from a clean checkout with no manual setup beyond having a
  C compiler.
- `research/decisions.md` records the host language with reasoning — done.

### Phase 2 — Interpreter core (M) — COMPLETE

Lexer, parser, tree-walking evaluator. Narrow language: integers, `bool`, string
literals, arithmetic, comparison, `if`/`else`, `while`, functions, `return`, and
variables (decision 13).

The point is to prove the pipeline end to end on a language small enough that
no design argument is needed. Resist adding features here.

**Exit criteria — all verified**

- `siyana run hello.sy` prints output — confirmed.
- A failing parse produces a diagnostic with line and column — confirmed, with
  the offending line echoed and a caret under the column.
- A runtime type error names the offending expression — confirmed.

Verified by 51 language tests plus 8 CLI contract tests, run on every build
under both Clang and GCC with `-Werror`.

**Beyond the criteria.** The stress program at
`tests/` exercises deep recursion and nested scopes; it was used to catch a
scope-teardown double-free that the shallow test cases missed.

**Known limits, by design.** The language slice is intentionally narrow and
does *not* include: return types (`fn f() -> int`), `string` as a declared type,
string concatenation, or any form of user-defined types. `print` is the only
builtin. All of these belong to Phase 3.

**Compiler coverage.** `-Werror` is on for every build. GCC caught two defects
Clang accepted: a misplaced `static` qualifier, and a `snprintf` that could
truncate. Both are portability bugs that would have surfaced later on other
toolchains, which is the argument for building with both compilers.

**[Unverified]** Memory leaks. AddressSanitizer is not available in this MinGW
installation (`libclang_rt.asan` is missing), so the leak check that should
accompany this phase has not been run. It should be enabled when a toolchain
with a sanitizer runtime is available, or on a POSIX host where ASan ships with
the compiler.

### Phase 3 — Semantic core (L) — IN PROGRESS

The real language. Types, tagged unions, error handling, and — critically —
the explicit allocator model.

The allocator decision from Phase 2 is where this project most easily goes
wrong, so it is designed rather than improvised. See "Allocator model" below.

**Exit criteria**

- A tagged union with a payload compiles and runs. — **met**
- An error propagates from a deep call stack with its context intact. — **met**
- An allocation failure is surfaced as a value, never a crash. — **met**
- A test suite exists that is expressive enough to write the OS in. — **partly**

**Done so far**

- Fixed-width integers: `Number`, `Number32`, `Word`, `Character` (decision 20).
- `Struct` declarations, literals, field read and assignment, value semantics
  (decision 21).
- `Union` declarations with payload-less and payload-carrying variants, and
  `Match` with exhaustiveness checking (decision 22).
- Error sets, `Result{T, E}` return types, `Try`, `Catch`, and `Fail`, with
  per-function error sets and two enforced rules (decision 23).
- Allocators with lexically scoped `allocator(x) { }` binding, handing out
  offsets, restoring on every exit path including error propagation
  (decision 24).
- Bitwise operators, shifts with per-signedness behaviour, hex literals, and
  digit separators (decision 25).

**The OS-expression probe**

Phase 3's last exit criterion is *a test suite expressive enough to write the OS
in*. Rather than assume, an OS-shaped program was written and run to find what
would not compile. That found the blocking gap (no bitwise operators at all — a
page table entry is `frame | flags << 48`) and produced the remaining list:

| Missing | Blocks |
| --- | --- |
| Arrays / lists | a page table is a collection |
| `String` as a type, length, concatenation | file paths, command lines |
| `const` named constants | `PAGE_SIZE`, register bit masks |
| `enum` | device states, status codes |
| `SizeOf` | struct layout, which decision 22 requires to be specified |
| `For` | convenience only; `Loop` covers it |
| Compound assignment (`<<=`, `|=`) | convenience only |

Nothing on this list is a redesign. They are additions to a language whose hard
decisions are made, which is the state Phase 3 was supposed to reach.

**Remaining**

1. Arrays and a `String` type — the two that block writing anything substantial.
2. Named constants.
3. Buffers and borrows, which unblock the ergonomics the allocator model was
   chosen for. Allocators currently hand out offsets because nothing yet means
   "a reference to a struct" (decision 24).

**[Known defect]** Every struct and union value leaks its `fields` array.
`free_aggregate_fields` is reached only from the error paths inside
`evaluate_struct_init` and `evaluate_union_init`; nothing on the success path
frees, and `scope_destroy` releases the name but not the value. A program that
builds a struct in a loop grows memory without bound.

Recorded rather than fixed because the fix needs a decision, not a patch: a
`Value` holding a struct must be deep-copied when copied (decision 21) *and*
deep-freed when dropped, which puts that responsibility at every `Value` copy
site. Half of it — the copy half — is already done via `value_copy`. The free
half is not.

Survivable while the interpreter is a bootstrap tool that will itself be
replaced; not survivable in the language. Compounding it, the Phase 2 note
above still stands: no sanitizer runtime is available on this toolchain, so
this is reasoned from reading the code rather than measured. It should be fixed
before the allocator model lands, because that is when leak *detection* becomes
the programmer's job and they will need to trust the interpreter's own
diagnostics.

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

**[Settled]** The host language is **C**. Verified available: GCC 13.2 and
Clang 16, targeting `x86_64-w64-mingw32`, C11 builds clean. See
[decision 8](../research/decisions.md) for the reasoning and the accepted
costs.

Path A is therefore the sequence: C interpreter first, ported to Siyana Lang
in Phase 6.

### Target sequence, and why it looks odd

Starting with x86-64 rather than bare metal is deliberate. Bare metal means
writing a bootloader and dealing with mode switching before the language can
do anything observable — a long stretch of work with no feedback loop.
Compiling to a hosted target first gives a working `siyana` run command
immediately, and the *same* compiler code then emits freestanding binaries
later. The architecture supports both; the sequence just front-loads the
feedback.

This means Phase 4 targets hosted x86-64 and Phase 7 introduces the freestanding
path — the reverse of the end goal, and intentional.

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

Not yet decided, and each blocks a specific phase:

| Question | Blocks | Why it blocks |
| --- | --- | --- |
| Compile-time evaluation design | Phase 3 | Interacts with generics and const sizing; retrofitting is expensive. |
| Do error payloads exist? | Phase 3 | Changes the type system; decision 15 leaves it open deliberately. |
| Automatic vs. explicit arena deinit | Phase 3 | Genuine tension between safety and predictability; decision 14 leaves it open. |
| Closures vs. first-class function pointers | Phase 3 | Closures are in the deferred list but this decides whether deferral is even possible. |
| Module system | Phase 4 | Needed before the freestanding split. |

**[Settled]** Source extension (`.sy`), comment syntax, grammar shape
(decisions 10–12), allocator model (14), and error handling (15).

## What to do next

Phases 1 and 2 are complete. Phase 3 — the semantic core — is under way: the
integer types, structs, tagged unions, and `Match` all build and are tested.

The two decisions that blocked Phase 3 were made via prototypes rather than
argument:

- **Allocator: lexically scoped binding** (decision 14). Verified by an
  executable prototype covering nested blocks, early return, and double pop;
  the harness was confirmed to fail against a deliberately broken restore.
- **Error handling: error unions with declared error sets** (decision 15).

Three decisions have been added since: structs are values (21), `Match` is an
exhaustiveness-checked statement whose unions have no guaranteed byte layout
(22), and the error syntax with its one deliberate divergence from Zig (23).

Next: settle compile-time evaluation — the last Phase 3 item that is a *choice*
rather than a build. After that the order is forced: the allocator block form,
because an error must be able to unwind past it and that mechanism now exists
but has nothing to unwind past yet.

**Still outstanding:** decision 12 (braces and semicolons) was explicitly
deferred as a "before Phase 3" item. The cost of switching grows with every
line written, and the parser is now the largest file in the bootstrap.