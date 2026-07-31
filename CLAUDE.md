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
scripts/diff-build origin/main   # what CI's build-and-test runs
scripts/format-all               # bazel + java + cc + scala formatters
scripts/mutation-check -f FILE -t 'TEST CMD' 's/OLD/NEW/'
bazel test //domains/<path>/...
```

Rust, Go, and TypeScript have **no formatting gate in CI**. Java formatting and
Bazel formatting do.

## Things that bite

- **`BUILD.bazel` `srcs` are listed by name**, with two globbing exceptions
  (`yochat_lib`, `wordchains_ios`). Elsewhere a new file not listed doesn't
  compile under Bazel and its tests don't run — while `go test ./...` or
  `npm test` still passes locally, because neither reads `BUILD.bazel`.
- **Postgres-gated suites skip silently** without `PG_TEST_DB_URL` /
  `GOLF_HUB_TEST_DB_URL`. CI supplies them from a `postgres:18` service; a
  local green run may have exercised no SQL at all.
- **Behind a proxy that 403s GitHub source archives** (cloud sandboxes, some CI
  runners), run `scripts/make-git-overrides.sh` once and import its output from
  `.bazelrc.user` — see [`docs/BUILD_AND_IDE.md`](docs/BUILD_AND_IDE.md). That
  gets the whole graph building, Beast and libpng and buildifier included.
  Without it every Beast-transport target fails to fetch, along with portrait
  and tracy_demo (the only libpng consumers); say so rather than reporting a
  skipped target as passing.
- **Never commit `MODULE.bazel.lock` churn** from the sandbox overrides — they
  run under `--lockfile_mode=off` for exactly this reason.
