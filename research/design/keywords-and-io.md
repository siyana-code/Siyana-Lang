# Keywords and I/O

Siyana spells its keywords out in full, and treats I/O as a first-class object
rather than a library call.

## Keywords

Full names, capitalised, chosen so that a Siyana program reads as prose and so
that keywords cannot collide with user identifiers at all.

| Keyword | Purpose | Replaces |
| --- | --- | --- |
| `Number` | The integer type | `int` |
| `Character` | The byte type | `char` |
| `Variable` | Declaration | `var` |
| `Function` | Declaration | `fn` |
| `If` | Conditional | `if` |
| `Else` | Alternative | `else` |
| `Else` + `If` | Chained conditional | `else if` |
| `Loop` | Repetition | `while` |
| `Return` | Early exit from a function | `return` |
| `True` | The boolean literal `1` | `true` |
| `False` | The boolean literal `0` | `false` |
| `Error` | Declare a set of failures | — |
| `Try` | Propagate a failure | — |

### Why full names

**No collision is possible.** `If`, `Loop`, and `Function` cannot be user
identifiers, so a program can never shadow a keyword and behave differently
from how it reads. With short keywords this is a real class of bug — a variable
named `if` in C, a function named `loop` in Rust.

**Grep works.** Searching a codebase for `Loop` finds every loop. Searching for
`while` also finds comments, strings, and documentation.

**The cost is real and worth naming.** Every loop costs four more keystrokes
than `while`. Every declaration costs seven more than `var`. For a language
meant to be written at speed this is a genuine ergonomic cost, accepted in
exchange for the two properties above.

### Chained conditionals

`Else` followed by `If` chains without introducing a new keyword:

```
If (a < b) {
    ...
} Else If (b < c) {
    ...
} Else {
    ...
}
```

### Example

```
Function Factorial(n: Number) -> Number {
    If (n <= 1) {
        Return 1;
    }
    Return n * Factorial(n - 1);
}

Variable result: Number = Factorial(5);
Terminal.Write(result);
```

## I/O as a first-class object

`Terminal` is not a library function. It is an **object** in the language with
methods, modelled on the device it names.

```
Terminal.Write("hello");
Terminal.Write(42);
```

The motivation is the OS target. A language meant to write an operating system
should treat I/O as something you *address*, not something you call into. The
shape anticipates:

```
File.Open("config.txt");
Device.Serial(1).Write(bytes);
```

where each of those is a device handle, not a namespace of global functions.

**[Synthesis]** This is the most novel thing in the design so far, and it is
also the least tested. Two risks:

1. **Over-committing early.** If `Terminal` grows to mirror real terminal
   semantics (raw mode, cursor control, windows) it becomes a large subsystem
   with a large surface, decided before the language can express what it needs.
2. **Namespace versus value.** `Terminal.Write` looks like a module path. If it
   is actually a value, `Terminal` can be passed around, stored, and eventually
   be a real kernel object. If it is a module, it cannot. **This is unresolved
   and should be decided before `Terminal` is implemented**, because it changes
   the type system.

**Recommendation:** implement `Terminal` as a *value* from the start, even
though the first version has two methods. A module cannot become a value later
without breaking every program that used it.

## Reading input

```
Line Variable = Terminal.ReadLine();
```

`Terminal.ReadLine` returns a `String`. Reading raw bytes, and reading from an
arbitrary device, are deliberately **not** in the initial design — reading a
line is what nearly every program wants, and a kernel-mode program will want
`Device.Read` instead, which is a different object entirely.

**[Unresolved]** Whether `Terminal.ReadLine` returns a `String` or a
`[]Character`. Returning `String` is friendlier and hides the UTF-8
convention; returning the raw sequence is more honest about what came off the
wire. The instinct is `String`, since the convention already exists and is
checked by the library.

## Builtins versus library

Only `Terminal` is built in. Everything else — `File`, `Memory`, `String`,
`Bool` — lives in the standard library as derived types and functions.

This keeps the compiler small (the type system stays implementable in a year)
while the language surface grows freely, because library code does not require
compiler work.