# Siyana Lang

A general-purpose programming language, built for command-line tools, systems
programming, desktop applications, and operating-system-level work.

**Status: early design.** There is no compiler yet. This repository currently
holds the project vision and a roadmap. Everything below is a working document
that is expected to change.

## Motivation

Siyana Lang exists because of Siyana Code. The editor needed a language that
suits systems-adjacent work, and off-the-shelf options each forced a trade-off:
either the tooling was thin, or the language traded away control over memory,
layout, and build output.

The goal is a single language that is genuinely usable across the whole range —
from a small CLI utility to an OS component — without dropping to a lower-level
language at the bottom of the stack or giving up safety at the top.

## Design goals

These are the properties the language is meant to hold. They are stated as
goals because they are not yet all satisfied, and some of them are in tension
with each other by design.

- **Predictable performance.** No hidden allocation, no hidden control flow, and
  no unpredictability around representation. If the cost is not visible in the
  source, it should not be there.
- **Explicit memory control.** Ownership and lifetime managed by the programmer,
  not by a garbage collector. Memory errors should be prevented by construction
  where possible and diagnosable when not.
- **Small core, few builtins.** Language features should compose. A large
  builtin surface is avoided in favour of primitives that combine.
- **Fast compilation and fast programs.** Incremental rebuilds measured in
  milliseconds; tight, cache-friendly generated code.
- **Good error messages.** Errors should point at the mistake, explain it, and
  suggest a fix — a compiler that reads like a teammate.
- **Portable.** A stable target-independent model that maps cleanly onto
  multiple operating systems and architectures.
- **Scripting escape hatch.** Real programs need glue. A fast-to-compile,
  dynamically typed mode should be available for it without pulling the whole
  codebase into a second language.

## Non-goals

Stated up front so the scope stays honest:

- Not a scripting-first language. Startup and ergonomics matter, but this is a
  compiled systems language first.
- Not a replacement for a general-purpose managed runtime. Where a managed
  runtime is the better tool, it remains the better tool.
- Not a research project. Design should follow what makes the language useful in
  practice, not the other way around.

## Roadmap

Indicative only. No dates are committed to, and the ordering is more
significant than the timing.

### Phase 1 — Design

- Settle the core type model and memory model.
- Decide on the function and module system.
- Decide on error handling.
- Write the specification well enough to argue about, not just to implement.

### Phase 2 — Bootstrap

- Build the self-hosting path: get a minimal compiler written in Siyana Lang
  compiling itself. Self-hosting is the forcing function for a language, not an
  eventual nicety.
- Ship a working `hello-world` and a C-compatible FFI.

### Phase 3 — Real programs

- Memory allocator, containers, string handling.
- Build system and incremental compilation.
- Package manager.
- Aim the compiler at Siyana Code and eat its own dog food.

### Phase 4 — Depth

- Compile-time metaprogramming.
- OS-level primitives: drivers, syscall interfaces, embedded targets.
- Optional high-level mode for scripting and glue.

## Name

**TODO:** the etymology behind the name has not been settled. Worth recording
here once it is, since the name should point at what the language is actually
for.

## License

To be decided. Not yet chosen.

## Contributing

This project is in its design phase and there is nothing to build yet.
Discussion of the design is welcome via GitHub issues and discussions.