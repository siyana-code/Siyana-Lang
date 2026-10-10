# Siyana Lang

A general-purpose programming language, built for command-line tools, systems
programming, desktop applications, and operating-system-level work.

**Status: Phase 1 of 8 — scaffolding.** The architecture is designed and the
build works; the compiler does not yet interpret any Siyana Lang code.

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
phase: 1 - scaffolding
```

No language features are implemented yet. Running a source file reports that
plainly rather than pretending otherwise:

```console
$ ./build/siyana.exe hello.sy
siyana: cannot compile 'hello.sy' yet
siyana: the interpreter is not implemented (Phase 1 of 8)
```

## Layout

| Path | Contents |
| --- | --- |
| `bootstrap/` | C sources for the interpreter (Phases 1–5) |
| `src/` | Siyana Lang sources (Phase 6) |
| `lib/` | Siyana Lang standard library (Phase 3) |
| `tests/` | Test suite |
| `tools/` | Build scripts |
| `docs/` | Architecture plan and design documents |
| `research/` | Background material, sources, and decision log |

## Phases

| Phase | Deliverable | Effort |
| --- | --- | --- |
| 1 | Scaffolding, build, CLI | S |
| 2 | Interpreter core — lexer, parser, evaluator | M |
| 3 | Semantic core — types, unions, errors, allocators | L |
| 4 | Freestanding subset | M |
| 5 | CLI-capable standard library | M |
| 6 | Self-hosted frontend | L |
| 7 | Own backend — codegen, assembler, linker | L |
| 8 | Bootable kernel | L |

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