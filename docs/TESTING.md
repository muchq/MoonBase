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
than as a survivor, so a typo isn't mistaken for a finding. A malformed
expression is `INVALID` and carries sed's own complaint, which is a different
mistake from one that simply matched nothing. Either way the run exits
non-zero and says the run was incomplete: an expression that never applied is
a question that never got asked, and a green summary for it would be the same
false-clean report the baseline check exists to prevent (#1372). The file is
restored afterwards, including on Ctrl-C.

The test command runs in a subshell, so `-t 'cd some/dir && npm test'` is
fine — it cannot move the script out from under its own restore. That was not
always true: the script used to `eval` the command in its own shell against a
relative path, and a `cd` broke the restore *and* made every later mutation
report `killed`, because the second relative `cd` failed rather than the suite
catching anything (#1369). `scripts/test-mutation-check` pins both halves;
it runs in CI and takes under a second.

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

## Some bugs have no wrong answer to assert on

Mutation checking assumes the bug produces an observable difference. A whole
class of C++ defect doesn't: use-after-free, leaks, signed overflow, misaligned
access. The program is wrong and the assertions still pass — or it crashes
somewhere unrelated and you go looking in the wrong file.

```bash
bazel test --config=asan  //some/cc:target   # use-after-free, leaks
bazel test --config=ubsan //some/cc:target   # overflow, shifts, alignment
bazel test --config=tsan  //some/cc:target   # data races
```

These override the repo-wide `-c opt` with `-c dbg`, because a report whose
stack has been inlined away names the wrong function.

Two findings from the day these configs landed, both of which the ordinary
suite reported green on:

- `LRUCache::insert` mutated its map before its commit point, so a throw out
  of eviction left a map entry pointing into a freed list node (#1272). With
  the fix reverted, the ordinary build **segfaults with no gtest verdict at
  all**; ASan names it `heap-use-after-free` with both stacks, UBSan calls it
  a misaligned access.
- `png_plusplus` leaked the libpng struct on every failed encode, because its
  error handler threw across libpng's C frames and the constructor's cleanup
  never ran (#1274). All 22 tests in that suite passed; the leak report came
  after them.

The CI `sanitize` job runs all three, one matrix leg each, over the first-party
C++ tests whose dependency closure is also first-party — a build-cost boundary,
not a compatibility one. Running a heavier target locally is just the flag
above. The legs don't fail fast, because one sanitizer's finding shouldn't hide
the other two, and ASan and TSan can't be linked into the same binary anyway.

When a finding has an issue but no fix yet, its target carries
`tags = ["no-sanitize"]` with the issue number at the tag; the config filters
those out, and removing the tag is that issue's acceptance test.

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
