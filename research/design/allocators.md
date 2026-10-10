# Allocators — the Phase 3 slice

Decision 14 chose the *model* (lexically scoped binding) and
[prototype/allocator-model.md](../prototype/allocator-model.md) verified the
restore mechanics in C. This covers what an allocator actually is in a language
that has no pointers yet, and settles the automatic-vs-explicit deinit question
the prototype left open.

## The problem with starting here

The model assumes allocators hand out memory to code that outlives the call.
Siyana has no pointers. Decision 21 made structs *values*, and open question 6
(does a `Buffer` alias, and who owns it) is deliberately undecided — it was
deferred precisely because a pointer without an owner invites the leaks
decision 14 exists to prevent.

So the obvious first slice — allocators handing out `Buffer` handles — would
force that question as a side effect, before it has been asked properly.

## What this slice does instead

An allocator hands out **offsets into its own region**, not references. Reads
and writes go back through the allocator.

```
Error Memory { OutOfMemory, }

Variable arena: Allocator = Allocator(4096);

Variable start: Word = arena.Allocate(16) Catch {
    OutOfMemory { Fail OutOfMemory; }
};

arena.Store(start, 65);
Variable byte: Character = arena.Load(start);
```

This is real: `arena` owns real bytes, `Store` writes them, `Load` reads them,
and exhaustion is a value. Nothing is faked and nothing is hand-waved.

It is also exactly what a kernel does. A page allocator returns a frame number,
and the kernel accesses the frame through the allocator. That is not a
degenerate case — it is the case the target cares about most.

**What it does not do:** hand out something that outlives the allocator. A
`Buffer` handle is deferred to a later slice, and recorded as blocked on
open question 6 rather than quietly skipped. The cost is real: without a handle,
every access re-names the allocator, so the ergonomics Model C was chosen for
are not yet visible. That is stated rather than papered over.

## The binding

```
allocator(scratch) {
    Variable p: Word = Allocate(64) Catch { OutOfMemory { Fail OutOfMemory; } };
    ...
}                                  // binding restored here
```

`Allocate(n)` with no receiver allocates from the current binding. Without a
binding it is an error naming the fix, not a silent fallback to some global:

```
error: no allocator is bound; pass one explicitly or wrap this in an
       allocator(x) { } block
```

A silent default would be Model B — ambient state with no visible origin — which
is what decision 14 rejected. Requiring the binding to be written somewhere in
the source keeps the property that made Model C the choice.

`arena.Allocate(n)` remains legal everywhere. The block is not the only way to
allocate; it is the way to allocate *implicitly* from a chosen allocator.

## Restore on every exit path

The prototype verified the push/pop mechanics, but its check 3 only asserts that
the binding is *visible* inside the block — it has no error propagation to
restore through. So the property that actually matters was never tested there.

It is tested here, for real:

- normal exit from the block
- `Return` out of the enclosing function from inside the block
- an error propagating out of the block

All three must restore. The last is the one that was previously untestable, and
it is the reason the block form was ordered after error handling.

## Automatic versus explicit deinit

**[Unresolved in the prototype; decided here: explicit.]**

An arena's deinit is one operation freeing everything. Doing it automatically on
scope exit would be the safer default and matches the block form. It was
rejected:

Automatic deinit frees the region when the scope ends. A buffer allocated in
that scope and still referenced afterwards becomes a use-after-free — and the
language *created* the dangling reference rather than failing to prevent one.
In a kernel that is a page table pointing at freed physical memory.

Explicit deinit cannot cause that. It can cause a leak, and a leak is visible,
bounded, and the programmer chose it. Given that this language is being written
to run without a runtime that could catch either, choosing the failure mode
that is *loud* over the one that is *silent* is the whole point.

The cost is recorded: `allocator(x) { ... }` does not free anything. It binds.
Anything allocated inside it outlives the block unless something deinits it.

## Failure is a value

`OutOfMemory` is a variant of a built-in error set, so `Allocate` has a
declared signature without the user declaring anything:

```
Error Memory { OutOfMemory, }
```

is predeclared. Writing it again is a duplicate-name error, not a silent
redefinition — the same rule structs and unions already follow.

`Memory` is a *built-in* name in the same way `Number` is. It exists before the
program starts, it cannot be redeclared, and `OutOfMemory` cannot be given a
second meaning.

## Surface

| Form | Returns | Notes |
| --- | --- | --- |
| `Allocator(bytes)` | `Allocator` | A region of that capacity |
| `a.Allocate(n)` | `Result{Word, Memory}` | Offset of the first byte |
| `Allocate(n)` | `Result{Word, Memory}` | From the current binding |
| `a.Store(offset, byte)` | — | Out-of-range is an error |
| `a.Load(offset)` | `Character` | Out-of-range is an error |
| `a.Used()` | `Word` | Bytes handed out so far |
| `a.Capacity()` | `Word` | |

`Used()` is what makes the binding observable, and therefore testable. It is
how a test asserts that an allocation inside a block went to the scratch arena
and not the caller's.

Out-of-range `Store`/`Load` is a hard error, not a silent no-op or a wrap. A
kernel reading the wrong frame because of an off-by-one should stop.

## Ordering

Unchanged and now complete: errors first, allocator blocks second. An error
propagating out of an `allocator(x) { }` block must restore the binding, and
that needed exactly one unwinding mechanism to exist first.