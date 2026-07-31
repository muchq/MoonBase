# Working agreement

How work gets picked up, built, reviewed, and shipped in MoonBase. Written
down so a future session starts where the last one left off instead of
rediscovering the same conventions.

This is process, not architecture. Architecture lives in each domain's
`README.md` and in the per-service `docs/` directories (e.g.
`domains/games/apis/one_d4/src/main/java/com/muchq/games/one_d4/docs/`).

Adapted from the smithy-cpp working agreement. Where a convention there has no
MoonBase analogue it has been dropped rather than restated aspirationally, and
where MoonBase's tooling differs the MoonBase command is the one named.

## Shipping a change

**One item, one PR.** Take the highest-value open item, finish it, ship it,
then take the next. Don't batch unrelated fixes. A dependency bump and a
feature do not belong in the same PR.

**Altitude review first.** Before writing any code: read the cited code,
confirm the finding is actually real, and propose a plan. Only then implement.
Jumping straight to a fix hides the cases where the reported issue is a symptom
of something bigger — the "indexing is slow" report turned out to be
`chariot`'s `Board.play` costing ~0.8ms per move and ~99% of extraction CPU,
which no amount of tuning around the edges would have found.

**Ask about scope when the sizes differ materially.** If the plan has a minimal
version and a thorough version that lead to genuinely different work, ask —
with a recommendation, not a survey. If they only differ cosmetically, pick the
obvious one and say so.

**Question the request itself, not just how to build it.** Before implementing,
step back once and ask: is this feature a good idea? Is there another approach
that dissolves the problem instead of managing it? A request describes a
symptom the reporter noticed; it is not automatically the best response to that
symptom, and the person asking usually hasn't seen the constraint you're about
to read in the code.

The worked example is PR #1265. The request was "index requests from history
still show, and there's no way to tell if their data has been pruned." That is
a real problem, and it was built as asked: a per-request availability signal
resolved against `indexed_periods`. But the question nobody asked until
afterwards was *should those request rows be kept forever?* — and it is the
better question, because:

- Sweeping `indexing_requests` on the same clock as the data would have made
  the whole feature unnecessary; the row would vanish exactly when the data
  did. (It would also be worse UX — a request silently disappearing is a poorer
  answer than one labelled "Pruned" — so the signal survives the challenge. But
  that conclusion had to be *reached*, not assumed.)
- The table has no lifecycle at all. It grows forever, and `listRecent(50)`
  hides that from the UI, so nothing will ever surface it as a symptom. The
  feature request pointed straight at a gap that the feature does not close.

Raise the alternative in a sentence or two, give a recommendation, and proceed
— don't stall. If the alternative turns out to be the better design, that is a
much cheaper discovery before the code exists than after. If it doesn't, you
have a reason to record instead of an assumption.

**Don't open a PR unless asked.** Commit and push when the work is done; open
the PR only on request. Reference the tracking issue and, when the issue is a
checklist, tick the item once merged.

**Update the tracking issue.** Fold new data — reproductions, measurements,
scope corrections — back into the issue so it stays the source of truth. File
follow-ups for what you deliberately left out rather than leaving it implicit.

## Review panel

Before committing anything non-trivial, run a self-review panel:

- **Three independent agents, three distinct lenses.** Typically correctness
  and control flow; data access, SQL, and resource safety; and tests, docs, and
  CI gates. The lenses should barely overlap.
- **Each agent hunts, then tries to refute its own findings** before reporting.
  This is what keeps the signal-to-noise usable.
- **Verify the survivors yourself** before acting on them. Agents are sometimes
  confidently wrong; don't take a finding at face value.

**If the panel didn't run, say so.** A restart, an interrupt, or simply
forgetting can kill it. Report that plainly rather than letting the reader
assume the step happened.

**Answer review questions with tests, not paragraphs.** See the testing bar —
this is the single highest-leverage rule in this document.

**Look for simplification on every review.** A review that only hunts for
defects is doing half the job.

## Design and simplification

**The goal is simple, readable code with clear interfaces.** Not clever code,
not maximally general code — code the next reader understands without a tour.
An interface that takes a paragraph to explain is a design problem wearing a
documentation problem's clothes; fix the interface.

**Testability is a core requirement, not a side effect.** If something is hard
to test, that is a design defect, and the design is what changes — never settle
for testing it badly, testing it indirectly, or not at all. The seams that let
a test drive the behavior (an injectable clock, a store interface, a callable
policy) are part of the deliverable, not scaffolding bolted on afterward.

**Every review is a simplification opportunity.** Does this abstraction earn
its keep? Can two near-identical paths become one? Is this special case
actually special? Can this be deleted outright? The best review outcome is
often less code, not more.

**Characterize before you refactor — positive *and* negative tests, first.**
Before changing the shape of existing code, cover it with tests that pin both
what it does and what it *refuses* to do, and confirm they pass against the
unchanged code. Only then refactor.

The ordering is the whole point. Tests written afterward describe the new
code's behavior, not the behavior you meant to preserve — that is how a
refactor silently becomes a rewrite. And the negative half is not optional:
positive tests alone let a refactor quietly *widen* behavior, accepting input
the original rejected, which is exactly the shape of a security regression.

## Testing bar

**A test beats an argument.** If a behavior is interesting enough to question,
debate, or reason carefully about — in a review, in a PR thread, or in your own
head — write a test that runs in CI instead. Reasoning is invisible to the next
reader, decays as the code moves underneath it, and is exactly what the person
who wrote the bug already did. A test is executable, survives refactors, and
fails at the moment the property breaks rather than the moment someone notices.

In practice: when a reviewer asks "what happens if X?", the deliverable is a CI
test named after X — not a reply explaining why X is fine.

**Comments are not a contract. CI tests are.** A doc comment stating a rule
constrains nothing. It is intent, and intent that nothing enforces drifts from
the code the moment someone edits without reading it — silently, with no
failure anywhere. If a property matters, something must *fail* when it is
violated: a test, a type, or a fail-fast check that a test then pins.

MoonBase has the receipts:

- `API.md` documented `"errorMessage": null` in every `/v1/index` example. The
  service has never sent that key — the container's mapper omits nulls. The
  wire-compat test agreed with the doc because it built its own `ObjectMapper`
  instead of using the container's, so prose and test were consistently wrong
  together.
- `played_at` was bound through the JVM default zone. A row written under UTC
  and queried from an America/Los_Angeles JVM fell out of `month = "2026-06"`.
  Nothing said the column was UTC except a convention nobody enforced.
- `/v1/aggregate` silently truncated at its group limit, reporting 103 of 104
  games as though that were the whole answer.

Each was true prose and false behavior for as long as nobody tested it. Keep
writing comments — they carry the *why*, which no test can — but the comment
documents the contract; it is never the contract itself.

**The Beyoncé Rule: if you liked it, you should have put a test on it.** Every
observable behavior worth keeping gets a test, at every level that fits:

- **unit** — the mechanism itself;
- **integration** — the behavior through the real store, transport, or codec
  (`TestDb`-backed DAO tests, the `e2e/` suites against in-memory H2);
- **the consumer's boundary** — where the behavior is part of a contract
  someone else depends on, prove it the way they will actually hit it. That
  means raw JSON and raw frames, not a round trip through generated types that
  regenerate on both sides and hide a rename: `golf_hub_wire_test.cc`,
  `portrait_smithy_wire_test.cc`, `smithy_contract_test.cc`,
  `DtoJsonCompatTest`, `IndexResponseWireTest`.

An untested observable behavior is not a guarantee; it is a coincidence that
currently holds.

**Test through the same objects production uses.** A wire test that builds its
own serializer is testing the serializer it built. Pull the real one — the
container's `ObjectMapper` bean, the real DAO, the real router — and when
"the real one" is itself an inference, add one test that reads the actual bytes
off a real server.

**TDD for bug fixes.** Write the failing test first, watch it fail for the
right reason, then fix it. The zone bug above was pinned by a test running
under `TZ=Pacific/Kiritimati` that failed with
`expected: "2026-07-01 00:00:00" but was: "2026-07-01 14:00:00"` before the fix
landed.

**Mutation-test negative and security tests.** A test asserting that something
is *rejected* or *absent* must be proven to fail when the property it pins is
broken. `scripts/mutation-check` does this: it breaks the code on purpose and
reports which mutations the suite failed to notice.

```bash
scripts/mutation-check -f path/to/File.java -t 'bazel test //some:target' \
  's/count >= limit/count > limit/'
```

It refuses to run against an already-red suite and reports an expression that
matched nothing as `SKIPPED` rather than as a survivor. Mutate the *behaviour*,
not the syntax — a change that fails to compile says nothing about the tests.
See `docs/TESTING.md` for the longer treatment and two worked examples.

**Prove isolation with a control.** When a test asserts an absence or a
failure, add the positive twin that shares the fixture, so a broken fixture
can't masquerade as the property holding. `IndexResponseWireTest` asserts the
body contains `"status":"PENDING"` before asserting `"data"` is absent;
`indexResponseKeepsAnErrorMessageWhenThereIsOne` is the twin proving nulls are
dropped for the right reason.

**Corpus tests for anything that parses.** MoonBase has no fuzzing
infrastructure — that is a real gap, not a covered case. The standing substitute
is a frozen corpus of real inputs checked into the repo and replayed in CI:
`HikaruCorpusParityTest` replays 500 real chess.com games, pins the exact total
ply count, and checks each game's final position against chess.com's own
`[CurrentPosition]` header, so a tokenizer that silently drops moves fails.
Anything new that parses — ChessQL, PGN, frame codecs, URIs — should get the
same treatment.

**Re-run timing-sensitive tests.** Anything with threads, sockets, or an
embedded server gets `--runs_per_test=15` or so before it's trusted.

**Watch for tests that skip silently.** The Postgres-gated suites
(`env_inherit = ["PG_TEST_DB_URL"]`, `["GOLF_HUB_TEST_DB_URL"]`) do nothing
without the env var. CI supplies it from a `postgres:18` service; a local run
without it is green and has exercised none of the SQL. When you add a gated
test, say so in the PR body.

## Verification before pushing

Run these, and don't report success on a step that didn't run.

| Step | Command | Gated in CI? |
|---|---|---|
| Java formatting | `scripts/format-java` or `bazel run //:format` | yes — `format-check` |
| Bazel formatting | `bazel run //:buildifier` | yes — inside `scripts/diff-build` |
| C++ formatting | `scripts/format-cc` | **no** — script only |
| Scala formatting | `scripts/format-scala` | **no** — script only |
| Affected build + test | `scripts/diff-build origin/main` | yes — `build-and-test` |
| Web app | `npm run typecheck && npm test && npm run build` in the app dir | yes — `test-1d4-web`, on every PR |
| Deploy scripts | `scripts/test-deploy` | yes — `test-deploy`, `test-deploy-macos` |

`scripts/format-all` runs the bazel/java/cc/scala formatters together. Note
what it does **not** cover: Rust, Go, and TypeScript have no formatting gate in
CI at all, despite `rustfmt.toml` and `clippy.toml` existing. Don't claim
otherwise.

New source files must be added to the relevant `srcs` in `BUILD.bazel` by name.
Two subtrees glob instead — `domains/chat/libs/yochat_lib` and
`domains/games/apps/wordchains_ios` — but everywhere else a file that isn't
listed doesn't compile and its tests don't run, while a local `go test ./...`
or `npm test` passes because neither reads `BUILD.bazel`.

**Be explicit about what couldn't be verified locally, and why.** The sandbox
blocks the boost archive fetches (`boost.beast`, `boost.asio`) and some GitHub
source archives, so Beast-transport targets — aura's middleware test, the
golf_hub and portrait service binaries and their e2e suites — cannot build
here. See `scripts/bazel_restricted_egress.sh`. CI compiles them natively. When
you hit a limitation like that, prove it's pre-existing by reproducing it with
your changes stashed, then say so in the PR body.

## Docs

- Update docs in the same PR as the code: domain `README.md`, the per-service
  `docs/` markdown, and public contract comments.
- **When behavior changes, fix the doc that describes it in the same commit.**
  A doc left contradicting the code is a defect in its own right.
- Keep the claims accurate. Don't write that something is covered "everywhere"
  when a subtree is deliberately excluded; name the exclusion.
- MoonBase has no ADRs and no CHANGELOG. The nearest equivalents are the
  per-service design docs and the PR body; put the *why* in one of them rather
  than nowhere.

## Dependencies and infrastructure

**Re-check assumed limitations instead of repeating them.** A limitation
recorded in a previous session may no longer hold.

**When you find a workaround, make it reusable.** Document it in `docs/` and
add a script so the next session gets it for free.

**Dependency bumps: don't trust the PR's own green CI.** Check how stale its
base is — checks that passed against a base 50 commits old validated a tree
that no longer exists. Merge into current `main` locally and run the affected
suites. For a security-sensitive dependency, also ask what the existing tests
actually *assert*.

**A bump that clears an advisory may need more than the version number.**
Dependabot offered only an in-range `react-router-dom` bump for
GHSA-qwww-vcr4-c8h2, which could not clear it: the advisory covers
`7.12.0 – 8.2.0` and that package stops at 7.18.2. The fix required migrating
to `react-router` 8. Read the advisory's affected range against the available
versions rather than taking the proposed bump at face value.

**Bumps have fallout beyond compilation.** The `@cloudflare/vite-plugin` bump
needed for a transitive `sharp` fix also required raising
`compatibility_date`, because the newer plugin reaches the worker over RPC.
`npm run build` was fine; `npm run dev` no longer booted. CI only runs the
build, so nothing would have caught it. Run the app, not just its tests.

## Communication

- Raise a concern in a sentence or two, then proceed with the work. Don't stop
  and wait unless proceeding would be unsafe or wasted.
- Report outcomes faithfully: if a step was skipped, say it; if tests failed,
  show it.
- Distinguish real defects from nits when reviewing, and say which is which.
- Don't re-litigate decisions already made.

## Operational notes

- **Verify user-facing URLs by loading them.** `https://1d4.net/index` was in
  the docs and in the nav for months while returning a 307 to `/`; Cloudflare's
  default `html_handling` redirects `/index` because `index.html` exists. No
  amount of reading the router would have shown it. Chromium and Playwright are
  available in the sandbox.
- **Merge commits on the working branch.** After a PR merges, the branch is
  reset onto `main`, which brings GitHub's own merge commit along. It is
  already-published upstream history and must not be amended.
- **Squash-merged branches are deleted.** The local `origin/<branch>` ref then
  points at a commit that is not an ancestor of `main`, and a
  `--force-with-lease` push fails with "stale info". `git remote prune origin`
  and push fresh; don't force.
- **Wedged PRs.** The owner sometimes merges locally and pushes `main`
  directly, leaving the PR object open with phantom conflicts. Before believing
  a conflict, check whether the PR head is already an ancestor of
  `origin/main`; a push to the branch un-wedges it.
- **Never commit `MODULE.bazel.lock` churn** produced by the sandbox's
  restricted-egress stub.
