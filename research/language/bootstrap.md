# Compiler Bootstrapping

How a language gets a compiler that compiles itself, and why it matters.

## What bootstrapping actually is

**[Primary]** From Wikipedia's article on compiler bootstrapping: bootstrapping
produces a self-compiling compiler — a compiler written in the source
programming language it intends to compile. An initial core version, the
bootstrap compiler, is written in a different language (which could be
assembly); successive expanded versions are developed using this minimal subset
of the language.

**[Synthesis]** The value is not the flex. It is three concrete things:

1. **The language is proven sufficient.** Building a compiler is a large,
   performance-sensitive systems program. If the language can build one, it can
   build the sort of software its users write. Nothing else tests that claim.
2. **Dogfooding is permanent.** Every language improvement immediately
   benefits the tool that builds the language.
3. **No permanent foreign dependency.** The bootstrap compiler gets retired
   rather than becoming a permanent second implementation to maintain.

## The stage model

**[Primary]** rustc's development guide describes the standard three stages:

- **Stage 0** — the pre-compiled compiler and standard library, downloaded as
  a binary.
- **Stage 1** — built from current source code, using the stage 0 compiler.
- **Stage 2** — rebuilt using the stage 1 compiler, with the in-tree standard
  library.
- **Stage 3** — the same-result test: stage 2 and stage 3 should be identical.
  If they are not, something is wrong.

**[Primary]** GCC's classic three-stage bootstrap, from Tom Mewett's
explanation:

- Stage 1 — a minimal C compiler is built with the host (existing) compiler.
- Stage 2 — GCC is built with the host-compiled minimal compiler.
- Stage 3 — GCC is built again with the host-compiled GCC.

**[Primary]** On why there is a third stage: "The third stage, though
technically unnecessary from a correctness point of view, ensures that we get
the advantages of GCC code generation and optimisation in our compiler
executable. This could be quite important; we don't want our first minimal
compiler to be any more complicated than necessary."

**[Synthesis]** That last point is the general lesson. Stage 2 being correct is
the proof of self-hosting. Stage 3 being *optimised* is what makes the compiler
itself fast. Skipping stage 3 to save time means the compiler is correct but
sluggish, and a sluggish compiler compounds across the entire project's life.

## The three strategies

### Strategy 1 — Full bootstrap

Write stage 0 in C (or another available language), get it to compile the
compiler source, then retire stage 0.

**Cost:** the bootstrap compiler must be maintained in parallel until the
self-hosted compiler can replace it. Features get implemented twice.

### Strategy 2 — Zig's approach

**[Secondary]** From Zig's creator, Andrew Kelley, on Hacker News:

> "There's a third option, which is what Zig does. It has a 'bootstrap'
> compiler, written in C, which is kept in sync with the self-hosted compiler.
> Features are implemented twice, once in the bootstrap compiler, once in the
> self-hosted compiler. So the build process involves a fixed set of steps —
> stage1, stage2, stage3 — and the chain never grows more than this.
> No pre-existing binaries are required."

**[Primary]** The same source goes on the consequences:

> "No pre-existing binaries are required. Except for of course a C compiler."

**[Primary]** And a pointed reply in the same thread: "Zig doesn't need to"
(in answer to claims it does need pre-existing binaries).

**[Synthesis]** This is the most interesting of the three. It converts
bootstrapping from an open-ended obligation into a bounded one: the bootstrap
compiler is a fixed, understood artefact rather than a growing second
implementation. The cost is that every language feature must be written twice
for as long as the bootstrap compiler exists.

### Strategy 3 — Interpret-then-compile

Write an interpreter in an available language. Run the compiler source under
the interpreter. Once the interpreter handles the full language, compile the
compiler natively and retire the interpreter.

**Cost:** the interpreter must reach full language coverage anyway, and
interpreter performance makes the development loop painful.

### Comparison

| | Stage 0 written in | Twice-implemented | Pre-existing binary needed |
| --- | --- | --- | --- |
| Full bootstrap | C | Only until self-hosting lands | No |
| Zig-style | C, kept in sync indefinitely | Yes, permanently | Only a C compiler |
| Interpret-then-compile | Interpreter | Only until native compiler lands | No |

## Deeper bootstrap: the theoretical floor

**[Secondary]** For ambition: **hex0** was a bootstrap project writing a
compiler in hand-written machine code, tedious enough that it became
infamously difficult. **GNU Mes** took a different route — a Scheme
interpreter written in a minimal C subset, used to self-host, with the various
stage0-style projects eventually collaborating on a common base.

**[Secondary]** The [Onramp](https://github.com/ludocode/onramp) project
implements a portable self-bootstrapping C toolchain across handwritten machine
code and higher-level languages, bootstrapping itself once it reaches a
sufficiently capable VM.

**[Synthesis]** Not a realistic target for this project, and not worth
pretending to. The value of knowing it exists is calibration: every new systems
language is somewhere on a spectrum between "starts from C" and "starts from
hex0", and most should be candid about where they are on it.

## The lower bound: a compiler must compile something first

**[Synthesis]** There is an irreducible fact here. Before Siyana Lang can
compile anything, something must be able to compile Siyana Lang. Either that
something is Siyana Lang itself (requiring the machine-code bootstrap
approach), or it is a compiler in another language.

This is not a design choice to be optimised away. It is the boundary
condition every compiler project starts from. What *is* a choice is whether to
acknowledge it honestly or describe the project as self-hosted from the start.

## The chicken-and-egg problem with the standard library

**[Primary]** Notice in the rustc stage diagram that stage 0 includes both a
compiler *and* a standard library. A self-hosted build needs its own standard
library to be compiling at the same time as the compiler.

**[Synthesis]** Practical consequences for Siyana Lang:

- The bootstrap compiler needs enough of the standard library to compile the
  compiler. Defining that minimum honestly, early, prevents the compiler source
  from quietly depending on features the bootstrap cannot handle.
- Alternatively, write the compiler against a freestanding subset from the
  start. Given that freestanding capability is already a stated goal
  (see [landscape.md](landscape.md)), this is the cheaper path: the
  compiler becomes proof that the freestanding subset is real.

**[Synthesis]** That is a genuinely useful argument for the freestanding
constraint. It converts an abstract design goal into a concrete engineering
constraint that pays for itself.

## Recommendation

**[Synthesis]**

1. **Adopt the Zig strategy** — a bootstrap compiler in C, kept in sync. It
   bounds the obligation rather than leaving it open-ended, and it needs only a
   C compiler, which is the most portable prerequisite available.
2. **Write the compiler against a freestanding subset.** This makes the
   self-hosting constraint and the freestanding constraint the same constraint.
3. **Keep stage 3 in CI.** The same-result test is the only automated check
   that the compiler is genuinely correct rather than merely self-consistent.
4. **Be honest about stage 0 until it is retired.** Do not describe the
   project as self-hosted before it is.

## Sources

- [Bootstrapping (compilers), Wikipedia](https://en.wikipedia.org/wiki/Bootstrapping_(compilers)) — primary
- [What Bootstrapping does, Rust Compiler Development Guide](https://rustc-dev-guide.rust-lang.org/building/bootstrapping/what-bootstrapping-does.html) — primary
- [Bootstrapping and self-hosting, Tom Mewett](https://tmewett.com/bootstrapping-metacompiling) — primary
- [I never understood why "self-hosting" is considered a good thing, Hacker News](https://news.ycombinator.com/item?id=32533735) — primary for the quoted exchange
- [Bootstrap, Esolang Wiki](https://esolangs.org/wiki/Bootstrap) — secondary
- [Onramp, GitHub](https://github.com/ludocode/onramp) — secondary
- [What is Compiler Bootstrapping?, Language Lineage](https://www.languagelineage.org/guides/what-is-compiler-bootstrapping) — secondary