# Git and GitHub Conventions

Covers how this repository is branched, committed, reviewed, protected, and
released.

## Branching model

### The three candidates

| Model | Long-lived branches | Suits |
| --- | --- | --- |
| **Git Flow** (2010) | `main`, `develop`, plus `feature/*`, `release/*`, `hotfix/*` | Software with scheduled, versioned releases supporting multiple versions at once |
| **GitHub Flow** | `main` only | Continuous deployment; one version in production at a time |
| **Trunk-Based Development** | `trunk` only, no branching at all | High-performing teams with strong automated testing and feature flags |

**[Secondary]** Git Flow was popularised in 2010 for versioned-release
software. GitHub Flow and trunk-based development both emerged later, as
software delivery moved toward continuous deployment. Atlassian's analysis
notes Git Flow's long-lived branches "carry a higher risk of deviating from the
trunk branch and introducing conflicting changes."

**[Secondary]** TBD requires integration at least once a day, with incomplete
work hidden behind feature flags rather than living on a branch.

### Recommendation: GitHub Flow

**[Synthesis]** For this repository:

- One long-lived branch: `main`.
- Short-lived topic branches cut from `main`.
- Every change lands through a pull request.
- No `develop` branch.

Reasons specific to this project:

1. **A compiler has an unbreakable main branch requirement.** A `develop`
   branch exists so unreleased work can accumulate without blocking a release.
   Siyana Lang has no releases to protect yet, so `develop` would be ceremony
   with no payoff.
2. **The audience is likely small.** Git Flow's overhead is amortised across a
   release team. A single-maintainer project should not carry it.
3. **TBD's prerequisite is not met.** Trunk-based development needs automated
   testing and feature flags. There are no tests to run yet, so strict TBD
   would mean pushing to `main` with no safety net. GitHub Flow's pull
   requests provide a human checkpoint in the meantime.

**Revisit this when:** there is a stable 1.0, a release cadence, and CI that
gates merges. At that point trunk-based development becomes worth reconsidering,
and it is the more likely long-term answer.

### Default branch name: `main`, not `master`

**[Primary]** GitHub switched the default branch name for new repositories to
`main` on 1 October 2020, following a Software Freedom Conservancy statement in
June 2020 that the term "master" was offensive to some people. Existing
repositories keep their current name.

**[Primary]** Git 2.28 added `init.defaultBranch`, which is how the name is set
locally:

```
git config --global init.defaultBranch main
```

Git does not ship a deprecation warning for `master`, so the old name will keep
being produced by default unless configured. **[Secondary]** Bitbucket, GitLab,
and most forges followed GitHub.

## Branch naming

**[Synthesis]** Format: `<type>/<short-description>` in lowercase, words
separated by hyphens.

```
feat/lexer-error-recovery
fix/arena-alignment-on-64bit
docs/comptime-semantics
chore/ci-windows-runners
```

Rules:

- Slash-separated category, never a hyphen between type and description —
  hyphens within the description only.
- Present tense, imperative-ish, describing the change rather than the area
  (`feat/lexer`, not `feat/lexer-work`).
- Ticket references at the end when applicable: `feat/412-union-types`.
- No `develop`, `master`, or `release/` style names outside the short-lived
  topic-branch pattern.

## Commit messages: Conventional Commits 1.0.0

**[Primary]** Format, verbatim from the specification:

```
<type>[optional scope]: <description>

[optional body]

[optional footer(s)]
```

**[Primary]** The four structural elements:

1. `fix:` — patches a bug (correlates with `PATCH` in SemVer).
2. `feat:` — introduces a new feature (correlates with `MINOR`).
3. `BREAKING CHANGE:` — a footer, or `!` after the type/scope, introduces a
   breaking API change (correlates with `MAJOR`). May be attached to a commit
   of *any* type.
4. Other types are permitted. `@commitlint/config-conventional`, based on the
   Angular convention, recommends `build:`, `chore:`, `ci:`, `docs:`, `style:`,
   `refactor:`, `perf:`, and `test:`.

**[Primary]** Specification requirements worth honouring:

- Rule 1: type, then optional scope, then optional `!`, then required `: `.
- Rule 4: scope is a noun describing a section of the codebase, in
  parenthesis — `fix(parser):`.
- Rule 6: the body begins one blank line after the description.
- Rule 8: footers follow the git trailer format — word token, then `: ` or ` #`,
  then value.
- Rule 9: footer tokens use `-` in place of whitespace (`Acked-by`).
  `BREAKING CHANGE` is the exception.
- Rule 12: the footer token is uppercase `BREAKING CHANGE`, followed by colon
  and space.
- Rule 15: everything is case-insensitive except `BREAKING CHANGE`, which must
  be uppercase.
- Rule 16: `BREAKING-CHANGE` is synonymous with `BREAKING CHANGE`.

**[Primary]** Reverts are explicitly left to tooling: the spec "does not make
an explicit effort to define revert behavior." The suggested convention is a
`revert:` type with a `Refs:` footer naming the reverted SHAs.

**[Synthesis]** Scopes for this repository, matching planned directory layout:

```
feat(lexer): add contextual keyword recognition
fix(memory): correct arena growth for zero-length allocations
docs(research): add bootstrap stage comparison
chore(ci): cache compiler artifacts between runs
```

### A note on enforcing this

**[Synthesis]** Conventional Commits is worth adopting for the history it
produces, not for its tooling. Enforcing with `commitlint` on a solo project
adds a CI failure path for a rule that a careful author already follows. The
spec's own FAQ makes a related point: it recommends proceeding "as if you've
already released the product," on the reasoning that somebody is depending on
the result even during early development.

## Branch protection and rulesets

**[Primary]** GitHub rulesets have largely superseded classic branch protection.
The available rules, from GitHub's documentation, include:

- Restrict creations, updates, deletions
- Require linear history
- Require deployments to succeed before merging
- Require signed commits
- Require a pull request before merging
- Require status checks to pass before merging
- Block force pushes
- Require secret scanning alerts are resolved
- Require code scanning results
- Require code quality results
- Restrict file paths, path length, file extensions, file size
- Require merge queue

Notes from the documentation that affect real decisions:

- **Block force pushes and Restrict deletions are enabled by default.**
- **Require linear history** blocks merge commits, requiring squash or rebase
  merge. It is "selected by default" for restrictions, but the repository must
  allow squash or rebase merging before it can be turned on.
- **Required status checks come in two flavours.** *Strict* requires the branch
  to be up to date with the base branch before merging — more builds, safer.
  *Loose* does not — fewer builds, but status checks can fail after merge due
  to base-branch changes.
- **A status check can be pinned to a specific GitHub App.** Without this,
  anyone with write permission can set the state of any status check. Pinning
  means "if the status is set by any other person or integration, merging won't
  be allowed."
- **Merge queue** verifies each merge group against the current state of the
  base branch, rather than against a stale snapshot.

**[Secondary]** A practical gotcha: GitHub only offers status checks for
selection that have run on the protected branch within the last seven days, so
required checks cannot be configured until CI has run on `main` at least once.

### Applied to this repository

**[Synthesis]** A ruleset named `protect-main` is active on the default branch,
containing:

| Rule | Setting |
| --- | --- |
| `deletion` | Restrict deletions |
| `non_fast_forward` | Block force pushes |
| `required_linear_history` | Linear history, squash merge only |
| `pull_request` | 1 approval, dismiss stale reviews, require approval of last push, require conversation resolution |

**[Synthesis]** Not yet enabled, and why:

- `required_status_checks` — nothing to require until CI exists on `main`.
  Per GitHub's documentation and community reports, checks must have run on the
  protected branch within the last seven days before they can be selected, so
  this cannot be configured until the workflow exists.
- `merge_queue` — a single-author repository gains nothing and it adds latency.
- `required_signatures` — enable once there are outside contributors.

**[Synthesis]** On the Copilot caveat: when Copilot opens an unattributed pull
request, GitHub requires one extra approval by default, because the usual
assumption (the author and the approver are two different people) no longer
holds. Worth revisiting when automation is introduced.

**[Primary]** The ruleset is managed through the REST API. Note that the
`pull_request` rule requires `require_code_owner_review` to be present
explicitly — omitting it returns a 422 validation error rather than a default.

## Versioning: SemVer 2.0.0

**[Primary]** Increment MAJOR for incompatible API changes, MINOR for
backward-compatible functionality, PATCH for backward-compatible fixes.

**[Primary]** Critically for a pre-1.0 project: *"Major version zero (0.y.z) is
for initial development. Anything MAY change at any time. The public API SHOULD
NOT be considered stable."*

**[Synthesis]** This is the correct regime for the language and the compiler
until stability is actually demonstrated. Siyana Lang should ship `0.x.y`
releases and use `0.x.0` as the point where each minor version may break
things. Announcing `1.0` is a promise about compatibility that cannot honestly
be made yet.

## Pre-releases

**[Primary]** Pre-release versions have lower precedence than the associated
normal version (`1.0.0-alpha` < `1.0.0`). Identifiers of only digits compare
numerically; identifiers with letters or hyphens compare lexically in ASCII
order.

**[Synthesis]** Useful for Siyana Lang because a systems language invites people
to try it early. `0.1.0-alpha.1` sets an honest expectation that the language
may change underneath them.

## Repository hygiene

**[Synthesis]** Standard, non-controversial:

- `.gitignore` covering build outputs, editor cruft, and OS files.
- A `LICENSE`. Currently undecided — see the note in the top-level README. It
  cannot be retrofitted cleanly once people have built on assumptions.
- `CONTRIBUTING.md`, once there is something to contribute.
- A `CHANGELOG.md`, generated from Conventional Commits when releases begin.

**[Unverified]** Whether to adopt a `docs/` plus `research/` split, or keep
everything flat. Deferred until there is enough material to judge.

## Sources

- [Conventional Commits 1.0.0](https://www.conventionalcommits.org/en/v1.0.0/) — primary
- [Available rules for rulesets, GitHub Docs](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/available-rules-for-rulesets) — primary
- [Managing a branch protection rule, GitHub Docs](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/managing-a-branch-protection-rule) — primary
- [Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html) — primary
- [GitHub Flow, Atlassian](https://www.atlassian.com/continuous-delivery/continuous-integration/trunk-based-development) — secondary
- [Software Freedom Conservancy statement on branch naming](https://sfconservancy.org/news/2020/jun/23/gitbranchname/) — primary for the rename rationale