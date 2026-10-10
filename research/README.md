# Research

Background material for Siyana Lang, and the conventions this repository
follows. Nothing here is authoritative about the language itself — this is the
evidence base that decisions get argued from.

## Contents

| File | What it covers |
| --- | --- |
| [decisions.md](decisions.md) | Decisions made, with reasoning, including corrections and reversals |
| [prototype/allocator-model.md](prototype/allocator-model.md) | Three allocator models compared against the same program |
| [prototype/error-handling.md](prototype/error-handling.md) | Error unions vs. Result vs. out-parameters |
| [tooling/git-and-github.md](tooling/git-and-github.md) | Branching model, commit format, branch protection, CI, release |
| [language/landscape.md](language/landscape.md) | Survey of existing systems languages and what each proves or disproves |
| [language/bootstrap.md](language/bootstrap.md) | How a self-hosting compiler is actually built |
| [language/naming-conventions.md](language/naming-conventions.md) | Source-level naming rules the language should adopt |

## How to read these

Sources are labelled, and the labels mean something:

- **[Primary]** — the specification or official documentation. Quote it.
- **[Secondary]** — conference talk, well-regarded blog, or project announcement.
  Useful, but check before making an argument rest on it.
- **[Unverified]** — encountered while researching but not confirmed against a
  primary source. Treat as a lead, not a fact.
- **[Synthesis]** — my own inference. This is where opinions live, and it is
  also where errors are most likely. Disagree freely.

The last category is the one to watch. On the first pass at this repository a
confident-sounding etymology was written into the README without checking. It
was wrong. Anything marked **[Synthesis]** has not earned trust just by being
written down.

## Status

The repository is at design stage. There is no compiler. The architecture plan
lives in [../docs/architecture.md](../docs/architecture.md).