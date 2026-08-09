# MoonBase

A polyglot Bazel monorepo: Java, C++, Go, Rust, Scala, TypeScript, Swift.
Sources live under `domains/<domain>/{libs,apis,apps}`.

## Read this first

**[`docs/WORKING_AGREEMENT.md`](docs/WORKING_AGREEMENT.md)** — how work gets
picked up, reviewed, verified, and shipped here. It is the governing process
document; the highlights below are pointers, not a substitute.

The two rules that change behavior most:

- **A test beats an argument.** If a behavior is worth reasoning about, the
  deliverable is a CI test, not a paragraph. When a reviewer asks "what happens
  if X?", write the test named after X.
- **Run the review panel before committing anything non-trivial** — three
  independent agents on three distinct lenses, each refuting its own findings.
  If it didn't run, say so rather than letting the reader assume it did.

Other docs: [`docs/TESTING.md`](docs/TESTING.md) (mutation checking and the
traps that only surface in CI), [`docs/BUILD_AND_IDE.md`](docs/BUILD_AND_IDE.md),
[`docs/IMPORTING.md`](docs/IMPORTING.md).

## Commands

```bash
bazel test //domains/<path>/...   # verify your change — this is the local command
bazel build //domains/<path>/...
scripts/format-all                # bazel + java + cc + scala formatters
scripts/mutation-check -f FILE -t 'TEST CMD' 's/OLD/NEW/'
```

`scripts/diff-build` is **CI's** entry point, not a local one. Reach for `bazel
test` on the affected targets instead — see below for why this matters more than
it looks.

Rust, Go, and TypeScript have **no formatting gate in CI**. Java formatting and
Bazel formatting do.

## Things that bite

- **Don't run `scripts/diff-build` locally. It destroys uncommitted work.** It
  runs `git checkout <base> --force` and back to hash both revisions, and
  `--force` discards every modification to a tracked file. There is no prompt
  and no stash. Untracked files survive, which makes the loss look partial and
  reads like something else went wrong — you will go looking for the wrong bug.
  `bazel test` on the affected targets is the same build without the checkout,
  so there is no local job this script is the right tool for. "I want to see
  what CI will say" is not a reason: CI will say it. If you run it anyway,
  commit first — and note that wanting a green run before committing is exactly
  the impulse that loses the tree.
- **`BUILD.bazel` `srcs` are listed by name**, with two globbing exceptions
  (`yochat_lib`, `wordchains_ios`). Elsewhere a new file not listed doesn't
  compile under Bazel and its tests don't run — while `go test ./...` or
  `npm test` still passes locally, because neither reads `BUILD.bazel`.
- **Postgres-gated suites skip silently** without `PG_TEST_DB_URL` /
  `GOLF_HUB_TEST_DB_URL`. CI supplies them from a `postgres:18` service; a
  local green run may have exercised no SQL at all.
- **NullAway is on by default for all of `com.muchq`**, with exemptions listed
  one by one in `_NULLAWAY_LEGACY_OPT_OUTS` (`bazel/rules/java.bzl`). A new
  package is analyzed the day it exists; nobody has to remember to add it. The
  listed packages are legacy — they carried violations when the default flipped
  — and **that list only shrinks**: fix a package, delete its line. Adding a
  line fails `//bazel/rules:rules_test` until it is declared there too, on
  purpose.
- **No test source is analyzed anywhere**, including under annotated packages,
  because `java_test_suite` is a passthrough that adds neither the plugin nor
  the javacopts (the `java_test` macro beside it adds both; the repo uses
  `java_test_suite`). So a `@Nullable` mistake in a test is never caught.
- **Behind a proxy that 403s GitHub source archives** (cloud sandboxes, some CI
  runners), run `scripts/make-git-overrides.sh` once and import its output from
  `.bazelrc.user` — see [`docs/BUILD_AND_IDE.md`](docs/BUILD_AND_IDE.md). That
  gets the whole graph building, Beast and libpng and buildifier included.
  Without it every Beast-transport target fails to fetch, along with portrait
  and tracy_demo (the only libpng consumers); say so rather than reporting a
  skipped target as passing.
- **Never commit `MODULE.bazel.lock` churn** from the sandbox overrides — they
  run under `--lockfile_mode=off` for exactly this reason.
