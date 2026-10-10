# Siyana Lang

A general-purpose programming language, built for command-line tools, systems
programming, desktop applications, and operating-system-level work.

**Status: Phase 3 in progress.** A working interpreter for an
initial slice of the language. No compiler backend yet.

## The goal

Write a complete operating system in Siyana Lang, without depending on an
external toolchain.

This constraint is the design's main engine. An OS forbids every luxury — no
hidden allocation, no hidden control flow, no garbage collector, no runtime you
did not write, no exceptions. Every design question resolves to a yes or no
immediately, which is why it is worth choosing over the more crowded goal of
"a safer C."

More precisely: UEFI firmware does the mode switching and hands over a live
64-bit environment, so almost no assembly is needed, but a small entry point
must still be emitted. The honest goal is *no external toolchain, plus a few
bytes of entry glue* — see
[decision 9](research/decisions.md).

## Building

Requires a C11 compiler. No build system generator, no external dependencies.

```powershell
./tools/build.ps1            # build and test (debug)
./tools/build.ps1 release    # optimised build
./tools/build.ps1 clean      # remove build output
```

On POSIX systems, use `./tools/build.sh` with the same arguments.

`CC` is honoured if set; otherwise the script picks `clang` or `gcc`.

## Running

```console
$ ./build/siyana.exe --version
siyana 0.1.0
phase: 2 - interpreter core

$ ./build/siyana.exe run examples/hello.sl
Hello, Siyana!

$ ./build/siyana.exe run examples/tour.sl
--- literals ---
42
...
```

`examples/tour.sl` demonstrates everything the interpreter currently supports.

## The language so far

Four integer types — fixed-width, because an OS needs to express a network
header, an ioctl value, and a buffer size in one program. Keywords are spelled
out in full.

| Type | Width | Signed | For |
| --- | --- | --- | --- |
| `Character` | 8 | no | Bytes: memory, files, text |
| `Number32` | 32 | yes | Error codes, 32-bit ABI values |
| `Number` | 64 | yes | The default. Ordinary arithmetic |
| `Word` | pointer | no | Sizes, indexes, offsets, addresses |

```
// Line comments, and /* block comments */

Variable count: Number = 3;
Variable letter: Character = 'A';
Variable size: Word = 4096;
count = count + 1;

// Functions may declare a return type
Function Factorial(n: Number) -> Number {
    If (n <= 1) {
        Return 1;
    }
    Return n * Factorial(n - 1);
}

// Control flow
If (count > 3) {
    Terminal.Write("big");
} Else If (count > 1) {
    Terminal.Write("middle");
} Else {
    Terminal.Write("small");
}

Variable i: Number = 0;
Loop (i < 3) {
    Terminal.Write(i);
    i = i + 1;
}
```

Operators: `+ - * /`, `== != < <= > >=`, unary `-` and `!`. Builtins:
`Terminal.Write`, `Terminal.ReadLine`.

**Overflow is an error, never a wrap.**

```console
Variable n: Number32 = 2147483647;
Variable m: Number32 = n + 1;
```

```
error: overflow in Number32 arithmetic
```

This is the property that matters most for an OS. In C that expression silently
produces `-2147483648`, and a buffer size that wraps to `0` is a heap overflow.

**Mixed widths do not mix.** `Number32 + Number` is a type error naming both
sides, rather than a promotion rule that quietly changes the result's width.

**bool is `Number`.** `True` is `1`, `False` is `0`, and conditions use C
truthiness — so `If (42)` is True. A deliberate reduction in safety, chosen to
keep the type system small; the discipline lives in the library via
`Bool.assert`. See [decision 17](research/decisions.md).

**Structs are values.** `Variable b: P = a` copies. There is no aliasing to
reason about and no pointer syntax yet. See [decision 21](research/decisions.md).

**`Match` is exhaustiveness-checked.** A `Match` on a union with an unhandled
variant fails when it is reached. A tagged union that can be silently ignored is
worse than an integer tag, because the type looks safe while it is not. A tagged
union also has *no guaranteed byte layout* — on-disk and wire formats go
through a `Struct`, whose layout is specified. See
[decision 22](research/decisions.md).

**Bitwise, with the signedness that hardware needs.** `>>` is logical on `Word`
and `Character` and arithmetic on the signed types — `(1 << 63) >> 63` is 1, not
all ones. A left shift that carries bits out of the type is an error, not a
truncation, because a dropped flag bit is a security bug that compiles. See
[decision 25](research/decisions.md).

Deliberately **not** implemented yet: arrays, a `String` type, buffers, borrows,
slices, modules, and pointers.

**Allocation is explicit and failure is a value.** An `Allocator` owns a region;
`Allocate` returns an offset into it and `OutOfMemory` is a normal error to
handle. `allocator(x) { ... }` binds one for a block and restores the previous
binding on every exit path — including error propagation, which is the case the
model existed to get right. Deinit is explicit: the block frees nothing.
See [decision 24](research/decisions.md).

**Errors are values with declared sets.** `Try` propagates, `Catch` handles,
`Fail` originates. A call that can fail must be handled where it is written,
and a `Try` may only propagate an error the calling function declares it can
carry — otherwise an error could leave a function whose signature cannot
describe it. See [decision 23](research/decisions.md).

**`Catch` is exhaustiveness-checked — this departs from Zig on purpose.** Zig's
idiom is `catch |err| switch (err) { ... else => ... }`, where `else` keeps
compiling when a variant is added and quietly routes the new failure into a
fallback written for a different one. For a language that will write to disks
and hardware registers, that is a corrupt filesystem rather than a bad status
code.

Errors are reported with a source position and a caret:

```console
$ ./build/siyana.exe run broken.sl
broken.sl:1:10: error: expected  ';', found identifier
     |
1   | If (True) { Terminal.Write(1)
     |          ^
```

See [research/design/integer-types.md](research/design/integer-types.md),
[type-system.md](research/design/type-system.md), and
[keywords-and-io.md](research/design/keywords-and-io.md) for the full design and
its reasoning.

## Layout

| Path | Contents |
| --- | --- |
| `bootstrap/` | C sources for the interpreter (Phases 1–5) |
| `examples/` | Siyana Lang programs that run today |
| `src/` | Siyana Lang sources (Phase 6) |
| `lib/` | Siyana Lang standard library (Phase 3) |
| `tests/` | Test suite — CLI contract and language tests |
| `prototype/` | Executable prototypes backing design decisions |
| `tools/` | Build scripts |
| `docs/` | Architecture plan and design documents |
| `research/` | Background material, sources, and decision log |

## Phases

| Phase | Deliverable | Effort | Status |
| --- | --- | --- | --- |
| 1 | Scaffolding, build, CLI | S | Done |
| 2 | Interpreter core — lexer, parser, evaluator | M | Done |
| 3 | Semantic core — types, unions, errors, allocators | L | Next |
| 4 | Freestanding subset | M | |
| 5 | CLI-capable standard library | M | |
| 6 | Self-hosted frontend | L | |
| 7 | Own backend — codegen, assembler, linker | L | |
| 8 | Bootable kernel | L | |

Effort bands rather than calendar time: S is a few weeks of evenings, M a few
months, L a year or more. Calendar estimates for a solo project on this kind of
work are usually both wrong and demoralising.

The full plan is in [docs/architecture.md](docs/architecture.md).

## Design principles

- **Predictable performance.** If the cost is not visible in the source, it
  should not be there.
- **Explicit memory control.** Ownership managed by the programmer. No
  garbage collector.
- **Freestanding-capable.** The language's core value must survive removing
  libc, the OS, and the runtime. Zig, Odin, and Hare all treat this as
  non-negotiable; so does this project.
- **Small core, few builtins.** Features should compose.
- **Good errors.** Point at the mistake, explain it, suggest a fix.

## Name

**TODO:** the etymology behind the name has not been settled. Worth recording
here once it is.

## License

To be decided. Not yet chosen.

## Contributing

Phase 1. There is nothing to build yet, but design discussion is welcome via
issues and discussions — particularly the open questions at the end of
[docs/architecture.md](docs/architecture.md).