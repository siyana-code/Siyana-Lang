# Integer Types

Replaces the unqualified `Number` of decision 18. Research first, decision
second.

## What the research showed

**Hardware has no single word width.** From Laurence Tratt's *Static Integer
Types*:

> "Real hardware does not have a single 'word width': we need separate notions
> of address, data, and object widths. The stronger the guarantees one makes
> about the relationship between integer types, the harder it is to adapt to
> different platforms."

This is the finding that decides it. A single `Number` cannot be simultaneously
a pointer width and a 32-bit field, and a language targeting an OS needs both.

**Linux does not have a single integer type either.** From
`include/linux/types.h`: `u8`, `u16`, `u32`, `u64`, their signed equivalents
(`s8`...`s64`), `uint8_t`...`uint64_t`, `int8_t`...`int64_t`, and
`uintptr_t` for addresses.

**Rust does the same.** `i8` through `i64`, plus `isize` and `usize`, where the
size types are pointer-width by definition.

**The kernel style guide is explicit about why.** From
`kernel.org/doc/html/v4.10/process/coding-style.html`: *"Clear integer types,
where the abstraction helps avoid confusion whether it's `int` or `long`."*
The ambiguity is the problem, not the type count.

**Conclusion.** Fixed-width integers plus a pointer-width integer are not a
luxury of systems languages. They are the minimum for expressing a network
header, an ioctl struct, a memory address, and a buffer size in the same
program. Siyana's OS goal requires them.

## What this overrides

Decision 18 set `Number` and `Character` as the only primitives, with `Number`
carrying no width. **That is reversed.** The reason is specific, not
general: an OS cannot be written with an unqualified integer.

## The type set

Four integer types. Deliberately the smallest set that can express an OS, not
the full power-of-two ladder.

| Type | Width | Signed | Used for |
| --- | --- | --- | --- |
| `Character` | 8 | no | Bytes: memory, files, streams, text |
| `Number32` | 32 | yes | Error codes, `ioctl` values, 32-bit ABI compatibility |
| `Number` | 64 | yes | The default. Ordinary arithmetic |
| `Word` | pointer | no | Sizes, indexes, offsets, addresses |

### Why these four

- **`Character` stays 8-bit.** Already implemented, and in an OS the byte is the
  unit everything is measured in. Raw byte buffers are `[]Character`.
- **`Number` is fixed 64-bit, not pointer-sized.** Predictability: a `Number`
  is 64 bits on every target. This is what makes `Number` safe to serialise,
  hash, and store in a file format. Code that genuinely needs pointer-width
  arithmetic uses `Word`.
- **`Word` is pointer-sized and unsigned.** Sizes, indexes, and offsets into a
  buffer are non-negative by nature, so unsigned catches `index - 1` on an
  empty buffer instead of wrapping to a huge value. A negative offset needs
  `Number`, which is the correct place for it.

**[Note]** `Word` conflicts with C, where `word` is 16 bits on DOS and 32 on
32-bit x86 — it is *not* the C `word`. The name is chosen because it is the
traditional systems term for pointer-width, and it is documented here so the
ambiguity is deliberate rather than accidental. `usize` is the direct analogue.

### What is deliberately not included

| Type | Why not |
| --- | --- |
| `Number8`, `Number16` | `Character` covers 8-bit. 16-bit is rare outside hardware register banks, which are `Memory` operations, not arithmetic. |
| `Number128` | No OS target needs it yet. Linux guards it behind `__SIZEOF_INT128__`. |
| `Unsigned` variants of 32/64 | Can be added later **without breaking changes** — new types are additive. |
| Floating point | Not an integer question. Needed for the compiler, but not until a backend exists. |

**Adding these later is non-breaking.** That is the argument for keeping v1
small: the type system is extensible in the direction it needs to grow.

## Conversions

Never implicit. Same rule as `Number`/`Character` in decision 18, and for the
same reason: a silent narrowing conversion is a correctness bug that surfaces
as memory corruption rather than a type error.

```
Number.to_character(n)   -> Character or Error.OutOfRange
Character.to_number(c)   -> Number
Number.to_number32(n)    -> Number32 or Error.OutOfRange
Number.to_word(n)        -> Word or Error.Negative
Word.to_number(w)        -> Number
```

A conversion that would lose information returns an error rather than
truncating. `Number.to_number32(3000000000)` is an `Error.OutOfRange`, not
`1705032704`.

**[Unresolved]** Whether there is a lossless-widening implicit conversion
(`Number32` to `Number` always widens). Rust and C# allow it; Zig does not.
Recommendation: **allow it**, because `Number32` to `Number` is genuinely
lossless on every target and forcing a cast is noise. Everything else stays
explicit.

## Arithmetic

**Both operands must be the same integer type.** `Number32 + Number` is a
compile-time or run-time error naming both types.

This is stricter than C and Rust, and deliberately so. Mixing widths in C is
the origin of a large class of integer bugs, and the compiler can catch every
one of them here.

**Arithmetic yields the operand type.** `Number32 + Number32` is a `Number32`.
No promotion, because promotion is what makes widths quietly change.

## Overflow is an error, never a wrap

**Arithmetic that would overflow its type returns an error.** It does not wrap
silently.

```siyana
Variable n: Number32 = 2147483647;
Variable big: Number32 = n + 1;   // Error.Overflow
```

This is the single most important safety property in the whole type system for
an OS. A buffer size that wraps from `0xFFFFFFFF` to `0` is a heap overflow; a
file offset that wraps is a silent write to the wrong place. Zig checks
arithmetic overflow by default for exactly this reason.

**[Unresolved]** Whether `Number` (64-bit) overflow should be checked too, at
the cost of two extra instructions per operation. Recommendation: yes, checked
everywhere, and revisit only with measurement.

## Size

Fixed and predictable, which is what makes the layout rules meaningful:

| Type | Bytes |
| --- | --- |
| `Character` | 1 |
| `Number32` | 4 |
| `Number` | 8 |
| `Word` | 4 or 8, matching the pointer width of the target |

Layout has no padding. Structs are laid out in declaration order with no
alignment gaps beyond each member's own requirement — Phase 3 work, recorded
here because it constrains the type system.

## Summary

| Question | Answer |
| --- | --- |
| Integer types | `Character`, `Number32`, `Number`, `Word` |
| Default | `Number`, fixed 64-bit signed |
| Pointer width | `Word` |
| Unsigned | `Character`, `Word` |
| Implicit conversions | Widening only, if adopted; narrowing never |
| Mixed-width arithmetic | Error |
| Overflow | Error, never wraps |
| Extensible later | Yes — new widths are additive |