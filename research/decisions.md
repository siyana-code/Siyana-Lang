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
| 7 | Host language for the bootstrap interpreter: C | Settled | 2026-10-10 |
| 10 | Source file extension: `.sl` | **Supersedes** `.sy` | 2026-10-10 |
| 11 | Comments: `//` and `/* */` | Settled | 2026-10-10 |
| 12 | Grammar shape: braces and semicolons, C-shaped | Settled, **revisit before Phase 3** | 2026-10-10 |
| 14 | Allocator model: lexically scoped binding | Prototype verified | 2026-10-10 |
| 15 | Error handling: error unions with declared error sets | Prototype written | 2026-10-10 |
| 16 | Keywords spelled out in full | Implemented | 2026-10-10 |
| 17 | bool is `Number` restricted to 0/1, C-style truthiness | Implemented | 2026-10-10 |
| 18 | `Number` and `Character` are the only primitives | Implemented | 2026-10-10 |
| 19 | `Terminal` is a value, not a module | Prototype only | 2026-10-10 |

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

## 8. Host language for the bootstrap: C

**Decision.** The Phase 2–5 interpreter is written in C. `bootstrap/` reserved
for it.

**Reasoning.**

- **Portability.** C is the one language available on every machine that could
  plausibly run the build. Verified locally: GCC 13.2 and Clang 16 targeting
  `x86_64-w64-mingw32`, C11 builds clean.
- **Honestly matches the project.** A systems language whose bootstrap is C
  makes no pretense that C was a good choice for application code — it was
  chosen because it exists everywhere.
- **Vocabulary fit.** Pointer arithmetic, manual memory, explicit lifetimes map
  directly onto the language's own model, so the interpreter's code reads
  similarly to the Siyana Lang it will eventually implement.
- **Matches the ecosystem precedent.** Zig's bootstrap compiler is C, kept in
  sync with the self-hosted one. So is the LLVM-adjacent tradition generally.

**Costs, accepted.**

- Portability bugs in the interpreter surface as bugs in Siyana Lang itself.
  Mitigated by preferring standard C11 over platform extensions, and by
  keeping platform code isolated rather than scattered.
- The interpreter is written roughly twice: once in C, once in Siyana Lang.
  Mitigated by treating the port as a scheduled milestone, not an afterthought.

**Note.** This is the language used to *build* Siyana Lang. It is not a
recommendation about Siyana Lang itself, and it is not a constraint on the
design — only on the bootstrap path.

## 10. Source file extension: `.sy`

**Decision.** Siyana Lang source files use `.sy`.

**Reasoning.** Short, unambiguous, and does not collide with any existing
language. Made before the standard library exists, so no migration cost.

## 11. Comments: `//` and `/* */`

**Decision.** Both forms, matching C.

**Reasoning.** The author learned C first, and `//` is near-universal in modern
languages. Supporting only one form would be a restriction with no benefit;
supporting both is a small lexer change.

## 12. Grammar shape: braces and semicolons

**Decision.** Statement blocks use `{}`, statements terminate with `;`.

**Reasoning.** This is the single most consequential "feel" decision, and it
was made deliberately at the start rather than by accident.

**Arguments for.** Familiar to anyone who has written C, C++, Java, C#, Go,
Rust, Swift, or Zig. Error messages reference line and column, which only works
predictably when statement boundaries are unambiguous. The team that will read
this language is not hypothetical — it starts with one person who learned C.

**Arguments against, recorded honestly.** Real indentation-based syntax is
better in ways that matter: it removes the `;` requirement, it makes
misalignment visible, and it encourages consistent structure. Swift, Kotlin,
Nim, and Haskell all chose it. It is plausible that Siyana Lang would be better
with it.

**Deferral, not endorsement.** This decision is cheap to revisit now and
expensive to revisit after the standard library ships. It should be revisited
before Phase 3, while the language is still small enough that changing it costs
nothing. The cost of switching later grows with every line of code written.

**Note.** This is not a claim that braces are better. It is a claim about what
to do first, and an acknowledgement that the question is open.

## 16. Keywords spelled out in full

**Decision.** `If`, `Else`, `Loop`, `Function`, `Variable`, `Return`, `Number`,
`Character`, `True`, `False`, `Terminal`, `Write`, `ReadLine`.

**Reasoning.**

- **No collision is possible.** A user identifier can never be `Loop` or `If`,
  so a program cannot shadow a keyword and behave differently from how it
  reads. With short keywords this is a real class of bug.
- **Grep works.** `grep Loop` finds every loop and nothing else. `grep while`
  also finds comments and documentation.
- **Chained conditionals** use `Else` followed by `If`, introducing no new
  keyword.

**Cost, accepted.** Four more keystrokes per loop than `while`, seven more per
declaration than `var`. This is a real ergonomic cost for a language meant to
be written at speed.

## 17. bool is `Number` restricted to 0 and 1

**Decision.** No boolean type. `True` is `1`, `False` is `0`. Conditions use C
truthiness: zero is False, anything else is True. The discipline lives in the
standard library (`Bool.assert`), not in the compiler.

**Reasoning.** Chosen by the author to match C, where `int` serves as `bool`,
so the type system can be exactly two primitives.

**Consequences, stated plainly.**

- `If (1)` compiles. So does `If (42)`.
- `If ("x")` is a type error — a String has no truth value.
- `Function F(a: Number)` accepts `True`, because `True` *is* a Number.
- Type confusion between integers and booleans is the programmer's
  responsibility.

**[This reduces safety.]** The Phase 2 interpreter rejected `If (1)`. That check
was removed by this decision. Recorded explicitly so it is never mistaken for
an oversight. A compiler that catches this class of mistake is strictly safer;
Siyana trades that for a two-primitive type system.

**Mitigation.** `Bool.assert` in the library, opt-in. Nothing forces it to be
called, which is the tradeoff being made.

## 18. `Number` and `Character` are the only primitives

**Decision.** Two primitive types. Every other type — `String`, slices, `Bool`,
and anything else — is derived in the standard library.

**Reasoning.** Keeps the type system small enough to learn in an afternoon and
implement correctly in a year. Library code grows without compiler work.

**`Character` is a byte, not a Unicode scalar.** Files are bytes; a `Character`
that cannot round-trip through a file is the wrong foundation for a language
meant to write a filesystem. UTF-8 becomes a validated library layer rather
than a type-system assumption. The visible cost: `Character` is not a
user-visible character, and `"é"` is two bytes.

**Consequence.** `Number` and `Character` never mix implicitly. No widening.

**[Unresolved]** `Number` has no width. The language needs to express a
pointer width, a 32-bit counter, and a network-order field, and cannot with one
unqualified integer type. This is the weakest part of the design and blocks
Phase 3 implementation.

## 19. `Terminal` is a value, not a module

**Decision.** Design `Terminal` as a value with methods, not as a namespace of
global functions.

**Reasoning.** A language meant to write an operating system should treat I/O
as something addressed rather than called into. As a value it can later be
passed, stored, and become a real kernel object; as a module it cannot.

**Status.** Implemented as a keyword with `Write` and `ReadLine`, but the
underlying model is still a special case in the interpreter rather than a true
value. A module cannot become a value later without breaking every program
that used it, so this must be settled before `File` or `Device` are added.

**[Unresolved]** Whether `Terminal.ReadLine` returns `String` or
`[]Character`.

## 14. Allocator model: lexically scoped binding

**Decision.** Allocators are threaded like Zig (allocation is visible where it
happens), *plus* an `allocator(x) { ... }` block form that binds an allocator
for a block and restores the previous one on every exit path.

**Reasoning.** Three models were compared against the same program in
[prototype/allocator-model.md](prototype/allocator-model.md):

- **Threaded (Zig)** — honest but threads an allocator through functions that
  allocate nothing, which is the majority of them in a real call chain.
- **Ambient (Odin)** — clean, but hides which allocator a function uses. In a
  kernel that is the dangerous case: allocating from a temporary arena in a
  function believed to be long-lived. It also contradicts the language's own
  no-hidden-state principle.
- **Lexically scoped** — the only option that keeps both properties:
  allocation is visible where it happens, and functions that do not allocate do
  not carry an allocator parameter.

**Verified, not assumed.** [prototype/allocator_proto.c](../prototype/allocator_proto.c)
implements the binding and checks it, including the cases where the model
breaks: nested blocks, early return, and double pop. Nine checks, all passing.
The harness was confirmed to *fail* against a deliberately broken restore, so
the passing result means something. Built and run on every build.

**Costs accepted.**

- A new block form, and the compiler must restore the binding on every exit
  path. This is where bugs will live.
- No borrowed battle-testing: no existing language does this, so it needs more
  testing than a conventional design.

**Open.** Does `Arena` get automatic deinit on scope exit, or always explicit
`defer`? Automatic is safer and matches the block form; explicit is more
predictable. A genuine tension, to be decided explicitly rather than defaulted.

## 15. Error handling: error unions with declared error sets

**Decision.** Zig-style error unions, with the error set declared per function
(`error{OpenFailed, PermissionDenied}`). Callers may only handle errors in
scope; ignoring one is an error. A single `error{Any}` escape hatch exists for
FFI boundaries and genuinely open sets.

**Reasoning.** Compared in
[prototype/error-handling.md](prototype/error-handling.md) against Result types
and C-style out-parameters. This takes Result's static checking with error
unions' call-site ergonomics, and costs nothing at the type level — an error
set is just a set of tags.

Error unions suit the OS target specifically: a kernel's errors must be
*recoverable*, and an error-as-value can be inspected, logged, and returned
from. Exceptions and abort-on-error both force a policy decision at every
site.

**Interaction with decision 14.** An error propagating out of an
`allocator(x) { }` block must restore the binding. The two designs cannot be
settled independently, and the allocator block must therefore be implemented
*after* error propagation exists — one unwinding mechanism, not two.

**Open.** Do errors carry payloads? `error.ReadFailed` carrying errno is far
more useful than the bare tag, and this changes the type system. Should be
decided explicitly. Also open: whether `try` is a statement only (simple
grammar, visible failure paths) or also allowed inline (compact, hides
propagation).

## 13. Phase 2 language slice

**Decision.** Phase 2 implements only: integer literals, `bool`, arithmetic,
comparison, `if`/`else`, `while`, functions, `return`, and variables.

**Reasoning.** The purpose of Phase 2 is to prove the lexer → parser →
evaluator pipeline end to end on a language small enough that no design
argument is required. Adding features during this phase defeats that purpose.

**Noted gap.** The exit criterion "`siyana run hello.sy` prints output" implies
string literals, which are technically outside this slice. They are included
because an interpreter that cannot print text has not demonstrated a useful
capability, and because the diagnostic path is only meaningfully tested against
text output.

## 9. "No assembly" is not achievable; the honest version is

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

1. Compile-time evaluation: design and how much to expose. **Blocks Phase 3.**
2. Closures versus first-class function pointers.
3. Module system: file-based, package-based, or both. **Blocks Phase 4.**
4. Do error payloads exist? See decision 15.
5. Automatic versus explicit arena deinit. See decision 14.

**[Note]** The two decisions that blocked Phase 3 are now made (14 and 15).
Compile-time evaluation remains, and is the next item to settle before the
semantic core can be built.

**[Note]** Phase 1 — scaffolding — is not blocked by any of these. It can be
built now. Phase 2, the interpreter core, needs only a narrow slice of the
language (integers, `bool`, arithmetic, comparison, `if`, `while`, functions,
variables), none of which depends on the open decisions above.