# Tagged Unions and Error Propagation

Phase 3's core. This covers the two exit criteria that need new syntax:
a tagged union with a payload, and an error propagating from a deep call
stack with its context intact.

## Research: what real systems languages do

### Tagged union layout

Zig's `union(enum)` stores the tag plus the **largest** variant's payload. The
consequence, from a Zig layout study:

> `Value` over `i64`, `f64`, `u8` — `@sizeOf(Value) == 16`: 8 bytes payload
> plus 8 for the tag. **Even `small` takes 16 bytes** — the union is sized to
> the largest variant.

The tag defaults to the smallest integer holding all variants. An explicit
backing type (`enum(u2)`) gives byte-level control.

**A known cost, not a bug.** A union sized to its largest variant wastes space
when variants differ wildly. The mitigation is boxing the large variants behind
a pointer, which moves the problem to the allocator — and Siyana has an
explicit one, so boxing is honest rather than hidden.

### Tag placement is contested

Zig issue #1922 asks for control over *where* the tag sits, specifically so
that unions can be written to disk or sent over the network and read back
without serialisation. The request was declined, at least initially.

**Consequence for Siyana.** A tagged union's byte layout is **not
guaranteed**, so unions cannot be written to a file or a wire protocol
directly. Anything with an on-disk format uses a `Struct`, whose layout *is*
guaranteed. That is recorded as a rule rather than left to be discovered.

### Error propagation: what does not work

`setjmp`/`longjmp` is the C mechanism for propagating an error up the stack.
Two findings rule it out:

- **`longjmp` does not run cleanup.** From the primary literature on it:
  "`longjmp` is only safe if it's called at a time when the stack frame used by
  `setjmp` could not have possibly been overwritten." Cleanup registered
  between the `setjmp` and the `longjmp` does not happen.
- **SEI CERT ERR52** — "Do not use `setjmp()` or `longjmp()`" — the standard
  explicitly advises against using them to simulate exceptions.

**Why this is decisive for Siyana.** The project's model is an explicit
allocator (decision 14), where cleanup is the programmer's job and errors must
free what they allocated. `longjmp` would skip every deallocation on an error
path. In a kernel that is a leak per error; in a long-running process it is
eventual exhaustion.

This confirms decision 15: errors are values, propagated explicitly, with no
hidden control flow. An allocator binding being restored on the error path is
then ordinary code the compiler can see, not something a runtime hook must
remember to do.

## Tagged unions

```
Union Value {
    Nothing,
    Number(Number),
    Text(String),
}
```

Constructed by naming a variant:

```
Variable v: Value = Value.Number(42);
```

Destructured by `Match`, which is **exhaustiveness-checked**:

```
Match v {
    Value.Nothing { Terminal.Write("empty"); }
    Value.Number(n) { Terminal.Write(n); }
    Value.Text(s) { Terminal.Write(s); }
}
```

Every variant must be handled. A new variant turns every incomplete `Match`
into a compile error — which is the entire value of the feature. A union that
can be silently unhandled is worse than an integer tag, because the type looks
safe while it is not.

### Layout

| Property | Value |
| --- | --- |
| Size | Tag + largest payload, plus padding |
| Tag width | Smallest integer holding the variant count, minimum `Character` |
| Payload | Overlaps the other variants |
| Guaranteed? | **No** — see the rule below |

**Rule: a tagged union has no guaranteed byte layout.** Write it to a file or
a protocol through a `Struct`, which does. The tag's position is an
implementation detail that may change, and programs must not depend on it.

This is a constraint, not an oversight, and it is cheaper to write down now than
to discover when a wire format breaks.

### Cost, stated plainly

A union is as large as its biggest variant. `Value` over `Number` (8) and a
1000-byte buffer is 1008 bytes even when holding a number. Boxing the large
variant fixes it and costs an allocation — visible, because Siyana's
allocators are explicit.

**[Unresolved]** Whether `Match` should be usable as an expression returning a
value, or only as a statement. Statement-only keeps the grammar simple and
makes failure paths visible; expression form is more compact. Statement-only for
now.

## Errors

An error set is a union whose variants carry no payload:

```
Error Io {
    NotFound,
    PermissionDenied,
    OutOfMemory,
}
```

A fallible function returns `Result{Value, Io}` — success carrying `Value`,
failure carrying `Io`. `Try` propagates the failure to the caller; `Catch`
handles it:

```
Function Read(path: String) -> Result{Number, Io} {
    Variable handle = Try(OpenFile(path));
    Return handle.Size();
}

Variable size = Read("config") Catch |err| {
    Terminal.Write("failed: ", err);
    Return 0;
};
```

Two rules make this work:

1. **`Try` is explicit.** A call that can fail must be handled or propagated.
   There is no implicit propagation, so every failure path is visible in the
   source.
2. **`Catch` must handle every variant** the callee can return, or name the
   ones it handles and have a fallback. Same exhaustiveness property as
   `Match`.

### Why not exceptions

| | Exceptions | Error unions |
| --- | --- | --- |
| Cost when unused | Stack bookkeeping | Zero |
| Cleanup on unwind | Requires runtime support | Ordinary code |
| Visible in source | No | Yes |
| Works in a kernel | Awkward | Yes |

The cleanup row is the one that matters. With an explicit allocator, forgetting
to free on an error path is the bug an exception system would have caught for
you — and an exception system that *did* catch it would be doing hidden work,
which is what this language is against.

**[Unresolved]** Do errors carry payloads? `error.ReadFailed` carrying `errno`
would be far more useful than a bare tag, and it changes the type system. Phase
3's first version has no payloads; adding them later is additive if the
`Match` and `Catch` syntax already allows binding a payload.

## Implementation order

Forced, not preference:

1. **Structs**, because a `Result` is a struct and a union payload needs a type.
2. **Tagged unions**, because `Error` is a union and `Result` is built on both.
3. **Errors and `Try`/`Catch`**, last, because an error propagating past an
   allocator binding needs one unwinding mechanism, not two.

Doing errors first would mean building the unwinding twice.