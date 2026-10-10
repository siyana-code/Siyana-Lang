# The Siyana Type System

Two primitive types. Everything else derives from them.

## Primitives

| Type | Description |
| --- | --- |
| `Number` | 64-bit signed integer. The default. |
| `Character` | A single byte, 0-255. Unsigned. |

`Number` and `Character` are the only types with no dependency on another type.
Everything else is defined in terms of them, in the standard library, using the
derivation mechanism described below.

### Why bytes rather than Unicode

`Character` is a **byte**, not a Unicode scalar value. This is a deliberate
choice driven by the OS target:

- Files are bytes. A `Character` that cannot round-trip through a file is the
  wrong foundation for a language meant to write a filesystem.
- Terminals, network protocols, and memory are byte-oriented. Unicode as a
  first-class scalar (`char` in Rust) adds a width and an encoding assumption
  to the most basic type in the language.
- UTF-8 is a **layer**, not a foundation. It belongs in the standard library as
  a validated `String` and decoder functions over `[]Character`.

The cost: a `Character` is not a user-visible character. `"a"` is one byte;
`"é"` in UTF-8 is two. This is visible and is accepted, because the
alternative pushes an encoding policy into the type system where it does not
belong.

**[Unverified]** Whether this holds as the language grows. If user-visible
character handling turns out to be a constant source of confusion, widening
`Character` later is possible but would break the file round-trip property that
justifies it.

## Derived types

Every other type is derived by a declaration in the standard library. Nothing
is built into the compiler except the two primitives and the value types needed
to support them.

```
Bool     := Number(0 | 1)
String   := Sequence<Character> + length
[]T      := Sequence<T> + length
```

### Bool

`Bool` is `Number` restricted to `0` and `1`. There is no separate boolean
type, no boolean tag on values, and no distinct representation.

This follows C, where `int` serves as `bool`, and it is chosen for the
standard library to carry the discipline rather than the type system.

**Consequences, stated plainly:**

- `if (1) { }` **compiles and runs.** So does `if (42) { }`.
- `if ("a") { }` compiles only if `Character` and `Number` are
  interconvertible; they are not, so this is a type error.
- Type confusion between integers and booleans is possible and is the
  programmer's responsibility to prevent.

**[Reduces safety.]** The Phase 2 interpreter rejected `if (1)`. That check is
removed by this decision. A compiler that catches this class of mistake is
strictly safer; Siyana trades that for a simpler type system, matching C. This
is a deliberate reduction, recorded as decision 17 so it is not mistaken for an
oversight later.

**Mitigation, in the standard library:**

```
Bool.true      -> Bool      // always 1
Bool.false     -> Bool      // always 0
Bool.assert(v) -> Bool      // panics if v is not 0 or 1
```

`Bool.assert` is opt-in. Nothing forces it to be called, and that is the
tradeoff being made.

### String and sequences

`String` and arrays are sequences of elements plus a length. In the Phase 3
interpreter they are values; in the compiled backend they are pointer-and-length
pairs, which is what the freestanding subset requires.

```
String   : []Character, guaranteed valid UTF-8 by convention
[]Number : sequence of Numbers
[]T      : sequence of T
```

`String` and `[]Character` are layout-identical. `String` is distinguished only
by a convention that its contents are valid UTF-8 — a checked property of the
standard library, not of the type system.

## Type rules

**No implicit conversions between `Number` and `Character`.** They are distinct
types. Converting is explicit and falls on failure rather than truncating:

```
Number.to_character(n) -> Character or Error.OutOfRange
Character.to_number(c) -> Number
```

An OS language that silently truncated a byte value would undermine the
"predictable performance" principle at the most basic boundary.

**No implicit widening.** One numeric type means no integer-width promotion at
all. Width is a property of the storage location, not the type — see the note
on integers below.

**Equality is defined between matching types.** `1 == "a"` is a compile error,
not `false`. This is retained from Phase 2 and is not affected by the `bool`
decision.

## Integers and widths

`Number` has no width. Width belongs to the storage, not the type:

```
var small: Number = 5;         // inferred width where it is used
var bytes: [4]Number = ...;     // fixed width from the array
```

**[Unresolved]** This is the weakest part of the design and the thing most
likely to need revision in Phase 3. A systems language with a single
unqualified `Number` cannot express a pointer width, a 32-bit counter, or a
network-order field. The alternatives are a `Number8`/`Number16`/`Number32`/
`Number64` family (more types, more conversions) or a width parameter
(`Number(32)`, Zig-style). Both are workable; neither has been chosen.

This is recorded rather than deferred silently because it affects every
variable declaration, and Phase 3 cannot start without a decision.

## What the compiler knows

Deliberately minimal. The compiler understands:

- `Number` and `Character` as distinct types
- Function signatures with declared parameter and return types
- That `Number` arithmetic yields `Number`, `Character` arithmetic yields
  `Character`
- Assignment and initialiser type agreement

The compiler does **not** understand:

- Whether a `Number` is a `Bool` — the `bool` decision removes this
- Derived types, which live in the library
- Any notion of ownership, lifetimes, or aliasing

That last point is deliberate. Memory safety comes from the explicit allocator
model (decision 14), not from types. The type system stays small so it can be
learned in an afternoon and implemented correctly in a year.

## Summary

| Question | Answer |
| --- | --- |
| Primitives | `Number`, `Character` |
| Bool | `Number` restricted to 0/1, discipline in the library |
| String | `[]Character` plus a UTF-8 convention |
| Slices | Pointer and length |
| Implicit conversions | None |
| Width | **Unresolved** — see above |
| Ownership | Not in the type system; explicit allocators handle memory |
| Safety model | Allocator and error discipline, not types |