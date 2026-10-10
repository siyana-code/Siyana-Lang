# Error Handling — Prototypes

Second blocking decision for Phase 3. Compared against the same program:
a CLI program that reads a file, parses it, and reports failures.

This choice interacts with the allocator model (see
[allocator-model.md](allocator-model.md)): an error that unwinds past an
`allocator` block must restore the binding. They cannot be settled separately.

## The test program

```siyana
fn main(args: []string) -> int {
    var path = args.get(1) orelse {
        print("usage: tool <file>");
        return 2;
    };

    var content = read_file(path) catch |err| {
        print("cannot read ", path, ": ", err);
        return 1;
    };

    var parsed = parse(content) catch |err| {
        print("bad input: ", err);
        return 1;
    };

    print(sum(parsed));
    return 0;
}
```

Three failure points: a missing argument, an I/O error, a parse error. Each
wants different handling, and each is recoverable.

## Model A — Error unions

Errors are values. A function returns either a success or a failure, and the
caller must deal with it.

```siyana
fn read_file(path: []u8) -> ![]u8 {
    const handle = open(path) catch |e| return error.OpenFailed;
    return read_all(handle);
}
```

**Reading:** the error is an ordinary value flowing through an ordinary
return. No control-flow machinery. The type signature says "this can fail",
which is information a reader gets for free.

**Cost:** every call site needs a `try` or a `catch`, even when the failure is
genuinely impossible or genuinely unrecoverable. A deeply nested call chain
threads an error union through every function that does not handle it.

**Comparison with Zig:** identical. Zig's error unions are the best-known
instance and the one most obviously worth copying for a systems language.

## Model B — Result type

Success and failure are two variants of one explicit type.

```siyana
fn read_file(path: []u8) -> Result([]u8, IoError) {
    const handle = open(path) catch |e| return .failure(.not_found);
    return .success(read_all(handle));
}
```

**Reading:** more explicit than error unions — the caller destructures rather
than propagating. The error type is visible in the signature, which lets the
compiler check that a caller handles a failure it might produce.

**Cost:** noisier. Every call needs a `catch` that produces a `.failure(...)`,
rather than the bare `catch |e| return ...` of error unions. For a systems
language this verbosity shows up in every fallible function.

**Useful property:** the error type is in the signature, so a function that can
fail with `IoError` cannot silently be treated as infallible. Error unions carry
a looser guarantee — the specific error set is dynamic.

## Model C — Explicit result with an out-parameter

C-style: return a status code, write the value through a pointer.

```siyana
fn read_file(path: []u8, out: *[]u8) -> Status {
    ...
}
```

**Reading:** maximally explicit, no hidden machinery at all.

**Cost:** every fallible call needs a scratch variable for the output, and
failure handling is a branch rather than an expression. This is C, and it is C
for reasons that are mostly historical rather than good. It also forces a
choice of return convention that does not compose with values returned in
registers.

**Verdict:** rejected. It is strictly worse ergonomically for the same
information, and the OS target does not require C's conventions.

## Comparison

| | A: Error unions | B: Result | C: Out-parameter |
| --- | --- | --- | --- |
| Failure visible in signature | Yes | Yes | Yes |
| Error type known statically | No | Yes | No |
| Call-site verbosity | Low | Medium | High |
| Composes with expressions | Yes | Yes | No |
| Proven at scale | Yes (Zig) | Yes (Rust, Swift) | Yes (C) |
| Suitable for an OS | Yes | Yes | Yes |

## Recommendation

**Model A — error unions, with the error set constrained per function.**

Zig-style error unions, with one refinement worth making deliberately:

- **Every function declares its error set.** Not the whole dynamic union at
  runtime — an explicit `error{OpenFailed, PermissionDenied}` list in the
  signature.
- **Callers may only handle errors in scope.** Handling an error the callee
  cannot produce is a compile error; so is ignoring an error entirely.
- **A single `error{Any}` escape hatch**, for the cases where a genuinely open
  set is needed, mainly at FFI boundaries.

This is the union of A's ergonomics with B's static checking, and it costs
nothing at the type level — an error set is just a set of tags.

**Why not B outright.** Result types are proven (Rust, Swift) but their
call-site verbosity is real, and a systems language pays that cost more often
than an application language, because fallibility is everywhere: every syscall,
every allocation, every parse.

**Why this interacts with the OS.** A kernel's error handling has a property
application code does not: an error must be *recoverable*, not fatal. Error
unions make that natural — an error is a value you can inspect, log, and return
from — while unwinding exceptions and `abort`-on-error both force a policy
decision at every site.

## Risks

**Interaction with the allocator block.** An error propagating out of an
`allocator(x) { }` block must restore the previous binding. This is the
concrete reason the two designs must be implemented together, and why the
allocator block should be built *after* errors exist to unwind past.

**[Unresolved]** Do errors carry payloads? `error.OutOfMemory` alone is enough
for many cases, but `error.ReadFailed` is much more useful carrying the errno.
Zig allows error payloads. This is a real design question that changes the type
system, and should be decided explicitly rather than by default.

**[Unresolved]** Does `try` work inside expressions, or only as a statement?
Restricting it to statements keeps the grammar simple and makes failure paths
visible. Allowing it inline is more compact and hides propagation.