# Systems Language Landscape

What already exists, and what each existing language demonstrates about the
choices available to Siyana Lang.

## The design space

A systems language has to answer a small number of hard questions. Listing them
is more useful than any single comparison, because it is the list that has to be
answered.

| Question | Options in practice |
| --- | --- |
| Who owns memory? | Manual, arena, tracing GC, ownership/borrow-checked, hybrid |
| How are errors reported? | Returned value, error union, unwinding, abort |
| What is the generic mechanism? | Monomorphisation, comptime, macros, type classes |
| Is the compiler self-hosted? | Yes, eventually, or never |
| What does it compile to? | Native, LLVM, JVM, WASM, or several |
| What does it interface with? | C, POSIX, Win32, or nothing |

Every language below is a different answer to that table.

## Zig

**Status.** Andrew Kelley began it 8 February 2016. Still pre-1.0 and still
making breaking changes. Foundation-backed.

**[Primary]** Zig's stated design philosophy, in its own blunt phrasing: "There
is no hidden control flow, no hidden memory allocations, no preprocessor, and
no macros."

**[Primary]** The specific claims, as the language's own documentation puts
them:

- *No hidden control flow* — "If Zig code doesn't look like it's jumping away
  to call a function, then it isn't." The guarantee holds without needing to
  know the types involved.
- *No hidden allocations* — no `new`, and no language feature that implicitly
  allocates (string concatenation being the usual offender). "The entire
  concept of the heap is managed by library and application code, not by the
  language." Allocation takes an explicit `Allocator` parameter.
- *No preprocessor, no macros* — `comptime` instead. Any expression or block
  can be marked `comptime` and is evaluated during compilation using ordinary
  Zig: same syntax, same semantics, same standard library.
- *Error handling is hard to do incorrectly* — errors are values in an error
  union, propagated with `try`. The argument is structural rather than moral:
  if it is easy to ignore errors, developers reimplement the library rather
  than propagate errors correctly.

**[Secondary]** A widely circulated list of design principles attributed to the
Zig community style guide includes: communicate intent precisely; edge cases
matter; favour reading code over writing code; one obvious way to do things;
runtime crashes are better than bugs; compile errors are better than runtime
crashes; incremental improvements; avoid local maximums; reduce the amount one
must remember; memory is a resource; together we serve the users.

**[Secondary]** *Avoid local maximums* is described as the first principle:
rather than settling for the most comfortable solution, push past a perceived
limit toward the global maximum.

**What it demonstrates.** That a coherent, single-language answer to
metaprogramming is workable. `comptime` is the most interesting result here,
because it makes compile-time evaluation a property of the ordinary type
system instead of a separate macro language bolted on the side.

**What it demonstrates the cost of.** Pre-1.0 churn. The Odin forum makes the
practical point plainly: "Odin feels mostly done from a language standpoint
while Zig is still making big breakable changes every release." For a solo
maintainer, breakable changes to one's own language every release is a serious
ongoing cost.

## Odin

**Status.** Andrew Kelley also began Odin, after leaving on disagreement over
Zig's direction. **Unverified** the exact timeline of the split; verify before
citing.

**[Primary]** Odin aims for high performance while remaining simple to learn.
It shares surface syntax with Jai and uses only minimal compile-time
evaluation, relying on parametric polymorphism for reuse instead.

**[Secondary]** On memory: allocators are held in an implicit `context` rather
than threaded as parameters the way Zig does them. The Odin forum frames the
trade-off as Zig requiring explicit allocators — "much like C++" — versus Odin
tucking them away in the implicit context "which is lovely for composability."

**[Secondary]** On data layout: Odin's `#simd[N]T` is closer to a built-in
vector type, whereas Zig's `@Vector(N, T)` is closer to SIMD applied to normal
fixed-length arrays — the alignment requirements differ as a result.

**What it demonstrates.** That the two answers to the memory question —
parameterised allocator versus ambient context — are a genuine live
disagreement, not a settled point. Choosing badly here will be felt.

## C3

**Status.** Active, smaller community than Zig.

**[Primary]** From the C3 project's own comparison page:

- C3 uses macros to a modest degree; Zig's macro equivalent is far more
  pervasive.
- C3 does not depart from C as far as Zig does.
- Like Rust, C3 has slices as a first-class type.
- C3's standard library takes an explicit allocator, so it works with
  different allocation strategies.

**[Secondary]** On C interop: Zig's `@cImport` parses C headers directly, which
is powerful and widely praised, though one Odin forum commenter notes it "comes
with its own set of issues and edge cases." C3 does not import headers; you
declare what you need manually. The trade-off is automatic-and-fragile versus
explicit-and-boring. C3's framing of the goal is "AI compatibility and
straightforward FFI."

**[Secondary]** On ambition: Zig "gives you ultimate control even if that
control comes with complexity. C3 aims to give the developer minimal surprises
even if that means fewer advanced features."

**What it demonstrates.** The C-compatibility axis is a real design lever with
real consequences, not a binary choice. Being closer to C lowers migration cost
and raises familiarity; it also caps how much the language can improve on C.

## Hare

**[Secondary]** Appears in surveys as a Tier 1 language — meaning it is
designed so its distinctive features remain available in freestanding mode, the
property that actually matters for kernel and OS work.

**[Synthesis]** The "Tier 1 / Tier 2" distinction circulating in community
comparisons is genuinely the most useful axis for this project, and it is
barely discussed in mainstream language coverage. The criterion: does the
language's core value survive when you remove libc, the OS, and the runtime?
Rust's borrow checker works without std. Zig's comptime works without an OS.
Odin and Hare both keep their distinctive features in freestanding mode.

Since Siyana Lang targets OS work, freestanding-capability should be a
**hard requirement**, not a nice-to-have — and it should be a testable one,
meaning the standard library needs a genuinely freestanding subset.

## Carbon

**[Secondary]** A C++ successor project from the Google-sponsored ecosystem.

**[Secondary]** Appears in comparative surveys as good on syntax, pointers,
and control flow, but weaker on safety and memory — unsurprising for a project
whose premise is that C++ code keeps compiling.

**What it demonstrates.** That "fix C++" and "design a new language" are
different projects with different risk profiles. Carbon's bet is that
compatibility is the feature.

## The broader field

**[Secondary]** Other languages in the current conversation: **V** (Go-influenced
systems language with fast compilation), **Roc** (functional, aimed at product
teams), **Gleam** (type-safe, BEAM-hosted), **Mojo** (Pythonic ergonomics at
systems-level speed), **Nim** (Python-like syntax, manual memory management),
**Hare**, **C3**, **Inko**, **Pony**.

**[Synthesis]** The pattern across the failures is worth naming: the hard part
is rarely the type system. It is finishing. Languages stall at the point where
they need a working package manager, formatter, LSP, editor integration, CI,
debugger support, and documentation all at once. C3's absence of a stable LSP
is cited by users as a concrete reason for disinterest.

**Implication for Siyana Lang:** tooling is not the part to cut for speed. The
toolchain is what a language is judged on by anyone who is not writing a
compiler.

## What this means for Siyana Lang

**[Synthesis]** Conclusions, offered as positions to argue with rather than
settled decisions:

1. **Do not pick a side reflexively.** Every major axis above has at least two
   serious, shipped answers. Choosing one means choosing against a working
   alternative, so the reason should be written down.
2. **Freestanding capability is the distinguishing requirement.** It is what
   separates a language usable for OS work from a language merely fast at it,
   and it is cheaply testable. Zig, Odin, and Hare all treat it as
   non-negotiable.
3. **The self-hosting commitment is the real cost.** See
   [bootstrap.md](bootstrap.md). It is what keeps the language honest and what
   makes Zig-style churn permanent.
4. **C FFI is not optional.** Given the stated OS and desktop targets, C is the
   common denominator. The only real question is header-parsing versus manual
   declaration — and that question has real costs on both sides.
5. **Single maintainer changes the calculus.** Everything above assumes a
   maintainer or foundation. Without that, pre-1.0 churn and tooling breadth
   are the two likeliest failure modes. Both argue for a deliberately smaller
   language than Zig or Odin attempted.

## Sources

- [Why Zig When There is Already C++, D, and Rust?](https://ziglang.org/learn/why_zig_rust_d_cpp) — primary
- [Comparing Zig with Odin, Odin forum](https://forum.odin-lang.org/t/comparing-zig-with-odin/740) — secondary
- [C3 comparisons with other languages](https://c3-lang.org/faq/compare-languages) — primary
- [C3 vs Zig in 2025: Who's Really Fixing C?](https://www.youtube.com/watch?v=y3tDZACGRAY) — secondary
- [13 Languages Are Challenging C](https://dev.to/dimension-ai/13-languages-are-challenging-c-most-fail-only-five-stack-up-2no5) — secondary
- [Revisiting the design approach to the Zig programming language](https://sourcegraph.com/blog/zig-programming-language-revisiting-design-approach) — secondary
- [What does "hidden control flow" mean?, r/Zig](https://www.reddit.com/r/Zig/comments/18i3wmk/what_does_hidden_control_flow_mean/) — secondary
- [So Many New Systems Programming Languages II](https://colinsblog.net/2023-03-22-systems-languages-details) — secondary
- [Ask HN: What less-popular systems programming language are you using?](https://news.ycombinator.com/item?id=43223162) — secondary