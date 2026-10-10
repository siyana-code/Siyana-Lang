# Error Handling — Syntax

Decision 15 chose the *model* (error unions, per-function declared error sets).
This covers the *syntax*, which was left open. Two questions in
[prototype/error-handling.md](../prototype/error-handling.md) are settled here.

## What Zig does, precisely

`try` is not a separate mechanism. The Zig reference defines it as sugar:

> `try x` is a shortcut for `x catch |err| return err`

`catch` takes a block with the error bound to `|err|`, and is an **expression**:
it yields either the unwrapped success value or whatever the handler evaluates
to.

The idiomatic handler is a `switch` over the error:

```zig
action(req, res) catch |err| switch (err) {
    error.BrokenPipe, error.ConnectionResetByError => return,
    error.BodyTooBig => { res.status = 431; res.body = "Too big"; },
    else => { res.status = 500; },
};
```

## The finding that changes the design

**Zig does not enforce exhaustiveness of the error handler.** Nothing in the
language requires the `switch` to be complete. The idiomatic code above relies
on `else`, and every tutorial reaches for it.

From a guide to Zig's switch:

> The exhaustive nature of `switch` makes it pair nicely with enums… Be careful
> with the `else` clause though, as it'll match any newly added enum values,
> which may or may not be the behavior that you want.

That last clause is the whole problem. `else => { res.status = 500; }` compiles
today, keeps compiling when someone adds `error.TimedOut`, and quietly routes a
timeout into a 500. Nothing breaks. Nothing is reported. The bug surfaces in
production against a machine nobody tested on.

**For Siyana this is not acceptable, and the divergence from Zig is
deliberate.** An OS writes to disks, networks, and hardware registers; a
silently unhandled error is a corrupt filesystem, not a bad HTTP status. The
language already took the same position on `Match` (decision 22): exhaustiveness
is the entire reason to have a tagged union instead of an integer tag, and an
integer tag with a default arm is exactly what we are avoiding.

So `Catch` is exhaustiveness-checked. That is the single most important line in
this document, and it is the point on which Siyana knowingly departs from the
language it is otherwise modelled on.

## Syntax

### Declaring an error set

Already implemented — a payload-less union:

```
Error Io {
    NotFound,
    Denied,
}
```

### Declaring a fallible function

```
Function Open(path: String) -> Result{FileHandle, Io} { ... }
```

`Result{T, E}` is a return-type annotation only. It may not be used to declare
a variable, a parameter, or a struct field. See "What is deliberately missing."

### Propagating

`Try` is a **postfix operator**:

```
Variable handle = Open(path) Try;
Return Open(path) Try;
```

On success it yields the success value. On failure it returns the error from
the enclosing function — exactly Zig's `catch |err| return err`, but spelled so
there is only one form of it.

The prefix spelling `Try(expr)` is rejected. Two spellings for one operator is
one too many, and the postfix form composes: it can end a `Return`, sit on the
right of `Catch`, or initialise a variable.

### Handling

```
Variable size = Open(path) Catch {
    NotFound 0;
    Denied {
        Terminal.Write("denied");
        0
    }
};
```

An arm is **syntactically identical to a `Match` arm** — `Variant [binding] {
statements }` — with one addition: the last statement must be an expression, and
its value is the arm's result. That keeps one grammar for exhaustiveness rather
than two.

Note there is no `|err|` binding. The arms name the variants, which is the
information `err` would have carried. A binding slot (`NotFound(code) { }`) is
already in the grammar so that error payloads can be added without changing it.

### Exhaustiveness

`Catch` must handle every variant the callee can return. Adding a variant to an
error set turns every incomplete `Catch` of that set into a failure — the same
property as `Match`.

```
Function Open(path: String) -> Result{FileHandle, Io} { ... }

Variable f = Open("a") Catch {
    NotFound 0;      // Denied is now missing
};
```
→ `error: 'Catch' does not handle 'Denied' of 'Io'`

## Rules

1. A call to a function declared `-> Result{T, E}` must be immediately followed
   by `Try` or `Catch`. A bare call is an error naming the function.
2. `Try` is legal only if the enclosing function is itself declared
   `-> Result{_, E'}` and **every variant of `E` is present in `E'`**. The
   diagnostic names the missing variants.
3. A function declared `-> Result{T, E}` may `Return` a value of `T`. It may
   not return an error value directly — errors leave only via `Try`.
4. A function with no declared error set may not `Try` anything.
5. A `Catch` handler must be exhaustive over the callee's error set.

Rule 2 is the one that carries the weight. It is what makes "the error type is
in the signature" (the property that made error unions preferable to out-
parameters) actually true rather than aspirational.

## What is deliberately missing

**A variable of type `Result{T, E}`.** Every fallible call must be handled at
the point of the call. You cannot bind the whole union and deal with it two
lines later.

This is stricter than Zig and it is a real ergonomic cost. It buys one thing:
there is no path by which a `Result` reaches a place that does not handle it,
so rule 1 is not an approximation. If binding the whole union turns out to be
needed — and for an OS it may well be, for reordering fallible operations — the
syntax already leaves room, because the arms and the value production are
independent of where the result is stored.

**Error payloads.** `error.ReadFailed` carrying `errno` is far more useful than
a bare tag. Settled: **not in Phase 3.** A payload gives every variant its own
type, which is a type-system change, and the syntax being added here is already
the riskiest part. The binding slot exists so adding payloads later is additive
rather than a redesign.

## Runtime representation

Not a union. A `Result` needs two things a union cannot express — whether it
succeeded, and which *of two different declarations* the active side came from
— so it gets its own value kind holding `{ ok, Value, error Decl, variant }`.

A dedicated representation avoids synthesizing an AST node at run time, and it
keeps `Decl` meaning what it says: a declaration the user wrote.

Inside a `Catch` arm, `Match` is not involved. The arm's variant is known, and
the arm's statements run in a scope holding the recovery value.

## Ordering

Unchanged and still forced: errors first, then the allocator block. An error
propagating out of an `allocator(x) { }` block must restore the binding, and
that needs exactly one unwinding mechanism.