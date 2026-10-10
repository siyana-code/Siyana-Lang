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
| 20 | Fixed-width integers: `Number`, `Number32`, `Word`, `Character` | Implemented | 2026-10-10 |

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

**Consequence.** Integer types never mix implicitly. No widening.

## 20. Fixed-width integer types

**Decision.** `Number` (64-bit signed, the default), `Number32` (32-bit
signed), `Word` (pointer-width unsigned), `Character` (8-bit unsigned).
Arithmetic that overflows its type is an error, never a wrap.

**This reverses part of decision 18.** Decision 18 had a single unqualified
`Number`; that cannot express a pointer width, a 32-bit ioctl field, and a
buffer size in the same program.

**Reasoning.** From research on real systems languages:

- **Hardware has no single word width.** Tratt: "we need separate notions of
  address, data, and object widths."
- **Linux** uses `u8`/`u16`/`u32`/`u64`, signed equivalents, and `uintptr_t`.
- **Rust** uses `i8`..`i64` plus `isize`/`usize`.
- **The kernel style guide** says the goal is "clear integer types, where the
  abstraction helps avoid confusion whether it's `int` or `long`."

See [integer-types.md](design/integer-types.md) for the full research.

**Why four types and not ten.** The smallest set that can express an OS. Missing
widths (`Number8`, `Number16`, `Number128`, unsigned 32/64) are **additive** —
adding them later breaks nothing.

**Two guarantees that matter most for an OS:**

1. **Mixed-width arithmetic is an error.** `Number32 + Number` is rejected.
   Silently mixing widths is the origin of a large class of integer bugs.
2. **Overflow is an error, never a wrap.** A size that wraps from `0xFFFFFFFF`
   to `0` is a heap overflow; a wrapped file offset is a wrong-place write.
   This is Zig's behaviour and the single strongest safety property here.

**Cost, accepted.** Four integer types instead of one is more to learn, and
`Number32` is an ungainly name. Both are cheaper than a memory-corruption bug.

**[Unresolved]** Whether lossless widening should be implicit (`Number32` to
`Number`). Rust and C# allow it; Zig does not. Recommendation: allow, since it
is genuinely lossless on every target.

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

## 21. Structs are values, not handles

**Decision.** A `Struct` is a value type. Declaring, assigning, or passing one
copies it. There is no `&` or `*` in the language yet, so every struct is a
value and there is no reference form to special-case.

```
Struct Point { x: Number32, y: Number32, }
Variable a: Point = Point { x: 1, y: 2 };
Variable b: Point = a;
b.x = 99;              // a.x is still 1
```

**Reasoning.** Zig and C both take this route; Rust makes it the default and
then immediately complicates it with lifetimes. The alternative - reference
semantics - was rejected for three reasons:

- An OS spends its life passing structures around: a `PageTable`, a
  `RegisterFrame`, a `Task`. Copy semantics make that safe by default.
- With reference semantics, "who owns this?" needs an answer everywhere, and
  every answer is either a manual free or a borrow checker. This language has
  neither yet.
- Reference semantics are *additive* later - a `&T` can be introduced once
  lifetimes exist. Value semantics are *subtractive*: converting a
  reference-semantics program to values means revisiting every alias.

The cost is real and is recorded here rather than discovered later: copying a
large struct is expensive. An OS will want pointers eventually, and that is
what decision 22's `Word` is for.

**How this was verified.** The interpreter initially shared one `fields` array
between a variable and its copy, so `b.x = 99` silently changed `a` too. That
is the exact bug value semantics exists to prevent, and it is now a named test
(`a struct copies on assignment`).

## 22. `Match` is a statement and is exhaustiveness-checked at run time

**Decision.** `Match` destructures a union. Every variant must be handled, or
the program fails when the `Match` is reached. It is a statement, not an
expression.

**Reasoning.** Two properties, one syntax.

*Exhaustiveness* is the whole value of a tagged union. A union that can be
silently unhandled is worse than an integer tag, because the type *looks* safe
while it is not. Checking it when the `Match` executes rather than at parse
time means a value built dynamically cannot escape the check either - the
interpreter has no static types to prove against yet, and pretending otherwise
would give a guarantee it cannot keep.

*Statement-only* keeps the grammar flat and keeps failure paths visible. A
`Match` used as an expression would need a "what does an unhandled variant
evaluate to" answer, which is exactly the question exhaustiveness exists to
remove. Revisit if a case turns up where expression form is genuinely needed.

**A union has no guaranteed byte layout.** Recorded here because it is a
consequence, not a preference: tag placement is an implementation detail and
programs must not depend on it. Anything written to a file or a wire protocol
goes through a `Struct`, whose layout *is* specified. See
[design/tagged-unions-and-errors.md](design/tagged-unions-and-errors.md).

## 23. Error handling syntax, and the one deliberate divergence from Zig

**Decision.** Errors are values with per-function declared sets. `Try` is a
postfix operator that propagates; `Catch` handles locally and is
**exhaustiveness-checked**; `Fail` originates an error.

```
Error Io { NotFound, Denied, }

Function Open(path: String) -> Result{FileHandle, Io} { ... }

Function Read(path: String) -> Result{Number, Io} {
    Variable handle = Open(path) Try;          // propagate
    Return handle.Size();
}

Variable size = Open(path) Catch {
    NotFound { Terminal.Write("missing"); 0; }
    Denied {
        Terminal.Write("denied");
        0;
    }
};
```

Full reasoning and the rejected alternatives:
[design/error-syntax.md](design/error-syntax.md).

**The divergence, stated plainly.** Zig does not enforce exhaustiveness of an
error handler. Its idiom is `x catch |err| switch (err) { ... else => ... }`,
and `else` matches any variant added later. Nothing breaks when someone adds
`error.TimedOut`; the new failure is silently routed into a fallback written
for a different one.

Siyana rejects this. The language already requires `Match` to be exhaustive
(decision 22) on the same reasoning — a type that looks safe while it is not is
worse than no type — and an error handler is a `Match` with extra steps. For a
language that will write to disks and hardware registers, a silently swallowed
error is a corrupt filesystem rather than a wrong HTTP status.

**`Try` is postfix only.** Zig defines `try x` as sugar for
`x catch |err| return err`. Since Siyana has `Catch`, that is the whole
mechanism, so `Try` is spelled once: `expr Try`. It composes — it can end a
`Return`, sit under a `Catch`, or initialise a variable. The prefix spelling
`Try(expr)` was rejected as a second way to say the same thing.

**`Fail Variant;` exists because nothing else can start an error.** `Try` only
propagates. Without a way to originate one, no leaf function could ever fail and
the entire mechanism would be untestable. The variant is written bare rather
than `Io.NotFound` because the enclosing signature already names the set, and
there is only one.

**`Result{T, E}` may only be a function return type.** A variable of that type
would let a fallible call be bound and dealt with two lines later, and rule 1
below would become an approximation. This is stricter than Zig and is recorded
as a real cost.

### Rules

1. A call declared `-> Result{T, E}` must be immediately followed by `Try` or
   `Catch`. A bare call is an error naming the function.
2. `Try` is legal only if the enclosing function declares an error set covering
   every variant of the callee's. The diagnostic names what is missing.
3. A `Result` function may `Return` a value of `T`; errors leave only via `Try`
   or `Fail`.
4. A function declaring no error set may not `Try` or `Fail`.
5. A `Catch` must handle every variant the callee can return.

Rule 2 is what makes "the error type is in the signature" — the property that
made error unions preferable to out-parameters — actually true rather than
aspirational.

## 24. Allocators hand out offsets, and deinit is explicit

**Decision.** An `Allocator` owns a region. `Allocate` returns a **Word offset**
into it, not a reference, and reads and writes go back through the allocator.

```
Variable arena: Allocator = Allocator(4096);
Variable at: Word = arena.Allocate(16) Catch {
    OutOfMemory { Fail OutOfMemory; }
};
arena.Store(at, 65);
Variable byte: Character = arena.Load(at);

allocator(scratch) {
    Variable p: Word = Allocate(64) Catch { OutOfMemory { Fail OutOfMemory; } };
}                                  // binding restored here
```

Two spellings, one token: `Allocator` constructs, `allocator` binds a block.
Full detail: [design/allocators.md](design/allocators.md).

**Offsets, not references.** The obvious first slice hands out `Buffer`
handles, but Siyana has no pointers and open question 6 (does a Buffer alias,
and who owns it) is deliberately undecided — it was deferred precisely because
a pointer without an owner invites the leaks decision 14 exists to prevent.
Building allocators would have forced that question as a side effect.

Handing out offsets is not a degenerate compromise. It is what a kernel's page
allocator does: it returns a frame number, and the kernel accesses the frame
through the allocator. The target's most important case is the one this shape
serves natively.

**The cost is real and recorded.** Without a handle, every access re-names the
allocator, so the ergonomics Model C was chosen for are not visible yet. The
model is proven; the ergonomics arrive with buffers.

**Automatic versus explicit deinit: explicit.** The prototype left this open.

Automatic deinit on scope exit would free the region, and any buffer allocated
in that scope and still referenced afterwards becomes a use-after-free — the
language would *create* the dangling reference rather than fail to prevent one.
In a kernel that is a page table pointing at freed physical memory.

Explicit deinit can cause a leak. A leak is visible, bounded, and chosen. Given
a language meant to run with no runtime that could catch either, choosing the
failure mode that is loud over the one that is silent is the point. So
`allocator(x) { ... }` binds and restores; it frees nothing.

**`Memory { OutOfMemory }` is predeclared**, like `Number`. `Allocate` can then
declare a signature without the user writing anything, and a program cannot
redefine `OutOfMemory` with different contents and quietly change what
`Allocate` means.

**One deliberate exception to value semantics.** An `Allocator` aliases on copy:
two copies are the same arena. Copying an allocator would otherwise copy its
memory and hand out the same bytes twice. Structs copy (decision 21);
allocators alias; the distinction is in the type, not in folklore.

## 25. Bitwise operators, and how shifts treat sign

**Decision.** `&`, `|`, `^`, `~`, `<<`, `>>`, bound with C precedence. Hex
literals (`0xFF`) and `_` digit separators.

```
Variable flags: Word = 0;
flags = flags | (1 << 3);          // set
flags = flags & ~(1 << 3);         // clear
Variable frame: Word = pte & 0x0000FFFFFFFFFFFF;
```

**How this was chosen: by trying to write the OS.** A page table entry is
`frame | flags << 48`, and the probe for Phase 3's last exit criterion — *is the
test suite expressive enough to write the OS in?* — failed at the first line.
The gap list that came out of writing OS-shaped code and seeing what would not
compile was: bitwise operators, hex literals, `const`, `enum`, arrays, `String`
as a type, `SizeOf`, `For`, and digit separators. This is the blocking item; the
rest are recorded in the architecture plan.

### The three rules that carry the weight

**`>>` is logical on `Word` and `Character`, arithmetic on `Number` and
`Number32`.** Not a stylistic choice. `(1 << 63) >> 63` must be 1; an arithmetic
shift would make it 0xFFFF…FFFF, and every mask extraction in a kernel would be
wrong in a way that looks plausible. Signed types shift arithmetically because
that is what `-16 >> 2 == -4` means to a programmer, and it is what every real
compiler does.

**A shift count may be any integer type.** The one deliberate exception to
decision 20's "both operands must be the same type". A count is not data — it
is discarded the moment the shift is done — and `Loop (i) { flags << i }` with
`i: Word` is the ordinary shape of code that sets a register bit. The count
must still be non-negative and less than the shifted type's width.

**A left shift that carries bits out of the type is an error**, exactly like
arithmetic overflow. C would silently drop them, and a dropped flag bit is a
security bug that compiles.

### Constant expressions take the type they are used in

`flags | (1 << 3)` must work. Without this it is a type error naming a `Number`
meeting a `Word` — and the fix a programmer reaches for is a cast. A cast used
as a habit rather than a judgement is how widths get silently mixed, which is
exactly what decision 20 exists to prevent.

So three shapes are recognised as constants and take their context's type: a
bare literal, a shift of literals, and the complement of either. A complement
is additionally re-formed in the target width: evaluated at 64-bit *signed*,
`~(1 << 3)` is -9 and may not adopt `Word` — decision 20's protection, and it is
right — but `flags & ~(1 << 3)` must still mean "clear bit 3".

`Variable high: Word = 1 << 63;` is the same problem from the other side: 2^63
does not fit a signed `Number` but is an ordinary `Word`, so a constant in a
declaration is evaluated in the declared type.

### Also

An unsigned value prints unsigned. A `Word` of all ones is
18446744073709551615, not -1; printing it signed reports a mask as a negative
number and sends the reader looking for a sign bug that is not there.

## 26. Arrays are fixed-size and are values

**Decision.** `[N]T` is an array type; `[a, b, c]` is an array literal, whose
length must match. Indexing with `[i]` reads or writes an element, and `Length()`
gives the declared length.

```
Variable page: [3]Word = [10, 20, 30];
page[0] = 42;
Terminal.Write(page.Length());       // 3

Function Sum(table: [4]Word) -> Word {
    Return table[0] + table[1] + table[2] + table[3];
}
```

**How this was chosen: by trying to write the OS.** This was the first gap on the
list decision 25 produced. A page table is a collection, a register file is a
collection, and a frame buffer is a collection. Nothing substantial can be
written without one.

### Fixed size, not a growable list

A fixed length is what makes an array usable as a page table or a device
register block: the length is a property of the *type*, so it is known before
any value exists and it cannot change underneath a reader. `page.Length()` is
available on a variable declared with no literal at all, which is exactly the
case a kernel hits when it sizes a frame buffer from a constant it has not been
given yet.

A growable list is a separate feature, and it needs open question 6 answered
first - a list that reallocates has to say what a reference to it means, and
nothing in the language currently means "a reference". Adding arrays now without
solving that is the difference between making progress and creating something to
undo.

### Values, like structs

Decision 21 said structs are values because reference semantics are subtractive:
they are convertible later, and unconvertible. The same argument applies exactly
here, and `page[0] = 42` on a copy that the writer does not hold is a bug that
compiles and does nothing. So assignment copies the elements and passing to a
function passes a copy.

The cost is real and known: copying a 512-entry page table on every assignment
is expensive. It is the same cost C pays, it is visible in the source rather than
hidden behind a pointer, and `Word` is pointer-width so the copy is a memmove.
Optimising it later means introducing a borrow, which is decision 24's open
question 6 -- and an optimisation that can be added is better than a semantic
that has to be taken away.

### Bounds are checked on every access, and the check cannot be skipped

```
Variable page: [3]Word = [10, 20, 30];
Terminal.Write(page[3]);
```

```
error: index 3 is outside this array of 3 element(s)
```

This is the property that makes arrays safe to hand to code that was not written
to trust it. The language has no pointer arithmetic, so there is no
pointer-arithmetic mistake for a programmer to make -- which means the check
here is not defending against the programmer's mistake but against a bug in this
interpreter. A bounds check that could be switched off would be a check that
eventually gets switched off.

The check covers both ends, which is why a negative index is an error rather
than something counted from the other end. Clamping is worse than failing: it
turns an out-of-range access into a plausible-looking wrong answer.

### The length comes from the type, and the literal must match it

`Variable page: [3]Word = [10, 20];` is an error naming both counts rather than
a truncation or a pad. A literal that quietly loses elements is a way of
writing a page table that is missing an entry, and the missing entry is not the
part that looks wrong.

An array declared without a literal has every element **unset**, not zero. An
unset element prints as `nothing`. A page table whose entries are all zero is a
valid and very different thing - it maps everything to the null frame - and
zeroing an array would claim the programmer had chosen that.

### Deliberately not in this change

- **Arrays as struct fields.** Rejected for now, not in principle: a field's
  length would have to be part of the aggregate's type, which needs `SizeOf`,
  which decision 22 left open.
- **Nested arrays.** `[2][2]Word` is refused with a message saying where an
  array type may appear. An array of arrays is a two-dimensional structure with
  no OS use that a struct of arrays does not already cover.
- **Assigning to a nested index.** `a[0][1] = x` is refused. Only a whole
  variable is a target; a partial path is a thing to add deliberately rather
  than half-implement.
- **Slices.** A `[N]T` with no way to name a sub-range cannot be passed to a
  function that takes part of one, and that is a real limitation. It is the
  same missing piece as open question 6, and it is recorded there rather than
  solved twice.

### On the interpreter's own memory

Arrays add a third value type that owns a heap allocation, joining structs and
unions in the leak already recorded in `docs/architecture.md`. That is recorded
as a deliberate cost of moving forward, not fixed here. It should land before
buffers or pointers, because those make ownership load-bearing rather than an
implementation detail.

## Open decisions

Not yet made:

1. Compile-time evaluation: design and how much to expose.
2. Closures versus first-class function pointers.
3. Module system: file-based, package-based, or both. **Blocks Phase 4.**
4. Do error payloads exist? Settled **no** for Phase 3 — see decision 23. A
   payload gives every variant its own type, and the syntax being added was
   already the risky part. The binding slot `NotFound(code)` is in the grammar
   so adding them later is additive rather than a redesign.
5. Automatic versus explicit arena deinit. **Decided: explicit** — see
   decision 24.
6. Pointers and borrows. Decision 21 makes structs values and decision 24 makes
   allocators hand out offsets; nothing yet means "a reference to a struct."
   Decision 26 gives arrays value semantics for the same reason, which makes
   this the blocker for both buffers and slices -- a sub-range of an array has
   no way to be named yet.
7. May a variable hold a `Result{T, E}`? Currently no, deliberately. See
   decision 23.

**[Note]** The three decisions that blocked Phase 3 are now made (14, 15 and 26).
Compile-time evaluation remains, and is the next item to settle before the
semantic core can be built.

**[Note]** Phase 1 — scaffolding — is not blocked by any of these. It can be
built now. Phase 2, the interpreter core, needs only a narrow slice of the
language (integers, `bool`, arithmetic, comparison, `if`, `while`, functions,
variables), none of which depends on the open decisions above.