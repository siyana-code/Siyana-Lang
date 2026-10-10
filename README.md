# Siyana Lang

A general-purpose programming language, built for command-line tools, systems
programming, desktop applications, and operating-system-level work.

**Status: Phase 2 of 8 — interpreter core.** A working interpreter for an
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

$ ./build/siyana.exe run examples/hello.sy
Hello, Siyana!

$ ./build/siyana.exe run examples/tour.sy
--- literals ---
42
...
```

`examples/tour.sy` demonstrates everything the interpreter currently supports.

## The language so far

Implemented in Phase 2 — the interpreter runs this, and nothing beyond it yet.

```
// Line comments, and /* block comments */

// Variables need an explicit type
var count: int = 3;
count = count + 1;          // assignment

// Functions
fn fib(n: int) {            // no return types yet
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

// Control flow
if (count > 3) { print("big"); } else { print("small"); }

var i: int = 0;
while (i < 3) {
    print(i);
    i = i + 1;
}
```

Types available: `int` and `bool`. Operators: `+ - * /`, `== != < <= > >=`,
unary `-` and `!`. One builtin: `print`.

Deliberately **not** implemented yet, and coming in Phase 3 or later: return
types, `string` as a declared type, string concatenation, structs, unions,
enums, slices, modules, and the explicit allocator model.

Errors are reported with a source position and a caret:

```console
$ ./build/siyana.exe run broken.sy
broken.sy:1:10: error: expected  ';', found identifier
     |
1   | if (true) { print(1)
     |          ^
```

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