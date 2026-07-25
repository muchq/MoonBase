# Testing notes

Conventions worth knowing beyond "write tests". Most of this exists because
something got through.

## Check that tests can fail

A green suite proves the tests ran. It does not prove they would catch
anything, and the difference is easy to miss because both look identical.

The recurring shape is an assertion that holds for the right reason *and* for
a wrong one:

- A zero that means "healthy" and also "the query failed".
- A name that is correct whether or not the payload beside it belongs to the
  same object.
- A response asserted only by status code, leaving the body unprotected.
- A mock that ignores an argument, so a caller can pass the wrong value — or
  build no request at all — and every assertion still passes.

`scripts/mutation-check` breaks the code on purpose and reports which
mutations the suite failed to notice. A surviving mutation is a bug the tests
would ship.

```bash
scripts/mutation-check -f path/to/file.go -t 'go test ./...' \
  's/count >= threshold/count > threshold/' \
  's/x = ready \&\& check(y)/x = check(y)/'
```

It refuses to run against an already-red suite (every mutation would look
caught) and reports an expression that matched nothing as `SKIPPED` rather
than as a survivor, so a typo isn't mistaken for a finding. The file is
restored afterwards, including on Ctrl-C.

Mutate the *behaviour*, not the syntax. A change that fails to compile is
caught by the compiler and says nothing about the tests. Aim at decisions:
boundaries, guards, which variable feeds a call, an omitted field.

Worth doing when a test protects something whose failure is silent — a health
signal, a version, a permission, an error path — and when reviewing tests
someone else wrote. Not worth doing on everything.

Two real examples from `prom_proxy`:

- The Prometheus mock returned a canned response for *range* queries without
  looking at the query string. A handler could build its queries from the
  wrong variable and produce empty charts in production; seven mutations
  survived until the mock was keyed on the query.
- A dedup test listed its fixture rows newest-last, so "keep the newest row"
  and "keep the last row" gave the same answer. It passed against an
  implementation that had no dedup logic at all.

## Bazel `srcs` are explicit

`BUILD.bazel` lists sources by name rather than globbing, and CI runs bazel
directly with no gazelle step (`scripts/diff-build`). A new file that isn't
added to the relevant `srcs` will not compile under bazel, and its tests will
not run — while `go test ./...` locally passes, because it never reads
`BUILD.bazel`. Run `bazel run //:gazelle` after adding files.

## Some things only break on the other platform

`deploy.sh` runs from a developer machine, and macOS ships bash 3.2 as
`/bin/bash` — no associative arrays, no `${x^^}`, no negative array indices.
Linux CI runs bash 5 and cannot reproduce any of it. `scripts/test-deploy`
therefore does both: a static check for bash-4-only constructs, and a
`test-deploy-macos` CI job that runs the suite against the real `/bin/bash`.

The general point: when correctness depends on the environment, a test that
only ever runs in one environment is evidence about that environment.
