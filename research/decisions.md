# Design Decisions

Decisions made about Siyana Lang, with the reasoning behind each. Recorded so
that later changes are visible as changes rather than as drift.

Format: date, decision, reasoning, status. An entry marked **Reversed** or
**Superseded** stays in the file — knowing what was tried and why it failed is
worth more than a clean-looking list.

## Constraints

| # | Decision | Status | Date |
| --- | --- | --- | --- |
| 1 | Memory model: explicit allocators, Zig-style | Settled | 2026-10-10 |
| 2 | Bootstrap: interpreter first, own backend later | Settled | 2026-10-10 |
| 3 | Codegen: own backend, no LLVM long-term | Settled | 2026-10-10 |
| 4 | Target: bootable OS with own toolchain | Settled | 2026-10-10 |
| 5 | Audience: solo maintainer, no stability promise | Settled | 2026-10-10 |
| 6 | Language scope: defined by a kernel-ready feature set | Settled | 2026-10-10 |

---

## 1. Explicit allocators, Zig-style

**Decision.** No hidden allocation. Functions that allocate take an allocator;
allocation failure is a value, never a crash.

**Reasoning.** Forced by the OS target — a kernel cannot assume a heap exists,
and cannot afford a collector. It also makes cost visible at the call site,
which is the project's recurring design principle.

**Open.** How the allocator is *passed*. Threading is honest but noisy;
ambient is clean but contradicts the no-hidden-state principle. A third
option — lexically scoped allocators — is worth prototyping. See
[architecture.md](../docs/architecture.md), "The allocator model".

## 2. Interpreter first

**Decision.** Build an interpreter before any compiler backend.

**Reasoning.** The interpreter settles language semantics cheaply. Codegen
work is high-risk and high-effort, and deferring it means the language is
proven usable before the expensive part begins.

**Consequence.** The first milestone is a CLI program running in Siyana Lang
(Phase 5 in the architecture plan), not a kernel.

## 3. Own backend, no LLVM

**Decision.** No LLVM dependency long-term. Write instruction selection,
register allocation, assembler, and linker.

**Reasoning.** Control over ELF output matters for an OS; LLVM assumes a
hosted, memory-managed world. Also serves the no-external-toolchain goal.

**Consequence.** This is the long pole. Register allocation dominates the
schedule. Mitigation: linear-scan allocation rather than graph colouring, and
a deliberately naive memory-only backend before any of it. See decision 6 and
Phase 7 in the architecture plan.

## 4. Bootable OS as the goal

**Decision.** The language's purpose is to be complete enough to write an
entire operating system.

**Reasoning.** Originally chosen without an articulated rationale. On
reflection this is the strongest available answer to "what is the gap?"
Most language authors never set this constraint, and it forces every design
question into a yes/no immediately — no hidden allocation, no hidden control
flow, no unwinding, no runtime. An OS forbids every luxury, which is what
makes it useful as a design forcing function.

**Correction.** "No assembly" is not quite achievable. UEFI removes nearly all
of it — firmware hands over a live 64-bit environment — but some entry glue
must still be emitted. See decision 7.

## 5. Solo maintainer

**Decision.** Primary user is the author. No stability commitment yet.

**Reasoning.** Determines everything about scope. It permits breaking changes
freely, removes the documentation and tooling obligations that consume
solo projects, and makes the two likeliest failure modes — pre-1.0 churn and
tooling breadth — ones to actively resist.

**Consequence.** No `commitlint` enforcement; Conventional Commits is adopted
for the history it produces, not for its tooling. Version as `0.x.y`.

## 6. Kernel-ready feature set

**Decision.** The language is defined by an explicit list of features
required to write an OS. Done means that list is complete — not that the
language is "complete."

**Reasoning.** The dominant failure mode is stalling at "90% done," because
90% of a language is unbounded. Every author can name three more features
that ought to be added, and none are individually wrong. An explicit
definition of done is the only reliable defence.

**Consequence.** Closures, async, GC, reflection, operator overloading, and
generators are all explicitly deferred. Each deferral has a real cost, noted
in the architecture plan.

## 7. "No assembly" is not achievable; the honest version is

**Decision.** The goal is stated as: a complete OS, no external toolchain, at
most a few bytes of hand-emitted entry glue.

**Reasoning.** Verified rather than assumed:

- **No linker: fully achievable.** chibicc compiles real C in ~10,000 lines
  emitting x86-64 assembly with no LLVM. Writing an assembler and linker in
  Siyana Lang is a known-achievable project.
- **No assembly: almost.** UEFI does the mode switching and page-table setup
  and hands over a live 64-bit environment. Rust demonstrates this with
  `x86_64-unknown-uefi`; the entire bootloader is a `no_std` crate.
- **The exception:** UEFI requires a specific entry point, so some glue must be
  emitted — directly from the compiler as bytes, not as assembly text.

**Correction.** This was originally recorded as an absolute "without assembly
or linker." It is wrong, and better to know now than at the end of the
project.

## Open decisions

Not yet made:

1. Host language for the bootstrap interpreter. C is the default candidate.
2. Error handling: error unions versus an explicit result type.
3. Compile-time evaluation: design and how much to expose.
4. Closures versus first-class function pointers.
5. Module system: file-based, package-based, or both.
6. Source file extension.