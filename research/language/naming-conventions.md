# Naming Conventions

Source-level conventions Siyana Lang should adopt, and the repository
conventions that support them.

Two separate topics live here. The first is how the *language* names things —
identifiers, files, modules. The second is how the *repository* names things.
They are related because the repository will eventually contain a lot of the
language's own implementation.

## Language-level conventions

**[Synthesis]** These are proposals, not settled decisions. Each is a default
that can be overridden by a good argument later. The important property is that
they exist before the standard library does, because retro-fitting naming onto a
shipped standard library is the most expensive kind of breaking change.

### Identifiers

| Kind | Case | Example |
| --- | --- | --- |
| Types | `PascalCase` | `ArrayList`, `FileMode` |
| Functions | `snake_case` | `read_file`, `parse_header` |
| Variables, parameters | `snake_case` | `buffer_len`, `is_valid` |
| Constants | `SCREAMING_SNAKE_CASE` | `MAX_ENTRIES`, `DEFAULT_CHUNK` |
| Module-level mutable globals | Defer to the language design | |

**Booleans** read as predicates: `is_valid`, `has_children`, `can_write`.
Not `valid_flag`, not `isValid`.

**Getters** are only named as getters when they compute or transform. If a
field is public, access it directly rather than writing `get_x()`. This follows
Zig's "favour reading code over writing code" — a one-line accessor adds a
layer to read through for no abstraction gained.

**Acronyms** are treated as words: `parse_http_request`, not `parseHTTPRequest`
and not `parseHttpRequest`. This keeps grep, case-insensitive search, and
identifier-boundary detection simple.

### Files and modules

**[Synthesis]**

- `snake_case` for files: `error_reporter.sy`, `lexer.sy`.
- One primary module per file, named to match: `lexer.sy` defines `Lexer`.
- A directory groups modules and has no file of its own.

**[Synthesis]** Rationale for `snake_case` over `PascalCase` for files: file
names appear constantly in error messages, build scripts, and diffs, and
`snake_case` composes better with shell usage and URL paths.

### Constants and immutability

**[Unverified]** Whether the language needs a distinct syntax for compile-time
constants, or whether `const` with a constant initialiser is sufficient. Zig,
Rust, and C all converge on something in this area, but the exact balance
between compile-time evaluation and a separate declaration form is an open
design question, not a naming one.

### Library layout

**[Synthesis]** A structure worth committing to early:

```
lib/std/       core standard library
lib/os/        OS interfaces (syscalls, processes, filesystems)
lib/cli/       argument parsing, terminal handling
lib/test/      test harness
```

**[Synthesis]** The `freestanding` boundary cuts across these. `std` must be
usable with no OS present; `os` is where OS access begins. Making that cut
explicit is what makes the freestanding property testable rather than aspirational.

## Repository-level conventions

**[Synthesis]** Mirrors what is written up in
[tooling/git-and-github.md](../tooling/git-and-github.md).

| Thing | Convention | Example |
| --- | --- | --- |
| Default branch | `main` | — |
| Topic branches | `<type>/<description>` | `feat/lexer-tokens` |
| Commits | Conventional Commits 1.0.0 | `feat(lexer): add token kinds` |
| Commit scopes | Match directory | `fix(memory): arena growth overflow` |
| Files | `snake_case` | `error_reporter.sy` |
| Rust host code | `snake_case`, modules `snake_case` | `error_reporter.rs` |

**[Synthesis]** Note that the commit scope vocabulary and the module
vocabulary are deliberately the same. A commit scoped `memory` should point at
something a reader would find under `memory` in the tree. Keeping the two
vocabularies aligned means `git log --grep` and directory browsing answer
roughly the same question.

### Directory layout

**[Synthesis]** Current:

```
research/          background material, sources, synthesis
```

**[Synthesis]** Proposed for the implementation phase:

```
compiler/          the compiler itself
lib/               standard library
tests/             test suite
tools/             build and CI scripts
docs/              user-facing documentation
research/          this folder
```

**[Synthesis]** One caveat: if the compiler is eventually self-hosting, it will
eventually need its own bootstrap compiler written in another language. Giving
that a distinct directory up front — `bootstrap/` — avoids restructuring the
tree at exactly the moment the bootstrap work starts, which is the worst time
to be moving directories.

## The principle underneath all of it

**[Synthesis]** Every convention here reduces to one of two things: make code
readable by someone who did not write it, or make the cost of change visible
early. Naming conventions serve the first. Commit and branch conventions serve
the second.

The ones that earn their keep are the ones that are cheap to follow and
expensive to retrofit. Everything else is optional.

## Sources

- [Why Zig When There is Already C++, D, and Rust?](https://ziglang.org/learn/why_zig_rust_d_cpp) — primary, for the design principles cited
- [Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/) — primary
- [Research index](README.md)