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

**Fold review feedback into the PR it came from.** When a review turns up
something small — a doc line that now contradicts itself, an assertion that
doesn't bite, a name that misleads — fix it in that PR. Don't file it. An issue
for a twenty-line fix costs more to write, triage, schedule and re-explain than
the fix does, and it lands on a reader who no longer has any of the context
that made the finding obvious.

This is in tension with "one item, one PR", and the tension resolves toward
folding, because the two rules are protecting against different costs and only
one of them is expensive here. Batching unrelated work makes a PR hard to
review; that is what the first rule is for. But a finding that came *out of*
this review is not unrelated to it — it is the review working. Splitting it out
buys nothing and spends the scarcest thing in the process: a reviewer who has
the code loaded right now. A second PR means a second full review pass, a
second CI cycle, and a second round of someone rebuilding the same mental
model, all so a diff can stay tidy in a way nobody will read it for.

Reach for a separate issue when the answer is genuinely unrelated to the change
under review, or when it is large enough to need its own design conversation —
the kind of question that would hold the PR hostage while it was argued out.
Both of those are real and both happen. But "it wasn't in the original scope"
is not one of them, and neither is "the commit would touch a third file."
Minimize round trips; use taste; when in doubt, fold it in and say in the PR
that you did.

The receipt is #1372, which should not exist. Review of #1371 turned up two
rough edges in the same script the PR was already fixing — an all-skipped run
reporting success, and an interrupt handler deleting a backup the exit handler
then restored from. Together they were about twenty lines. Filing them bought a
second issue to write, a second PR (#1373), a second review, and a second full
CI run, to land a change that would have been three paragraphs of the first
review thread.

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
- The table had no lifecycle at all. It grew forever, and `listRecent(50)`
  hid that from the UI, so nothing would ever have surfaced it as a symptom.
  The feature request pointed straight at a gap that the feature did not close.

That gap became #1266 and was closed in #1277, which gave the table a 30-day
window against the data's 7 — long enough that the availability signal above
still has something to report from, which is the conclusion this example says
had to be reached rather than assumed.

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

**Commit messages describe the change, not the process that produced it.**
State what changed and why a future reader would care — not a narrated account
of the session: no mutation-check kill lists, no "written before the change and
observed red", no confession that the review panel didn't run. That material is
real and belongs somewhere, but the somewhere is the PR description or the
tests themselves, not a message that ships into permanent history.

The receipt is 4a059fc: four squashed commits, each running several
paragraphs, re-deriving tradeoffs the diff already shows, quoting
`scripts/mutation-check` kill output verbatim, and reporting mid-message that
the review panel hadn't run. None of that is wrong to *know* — the panel
status belongs in the PR thread (see Review panel, below); the mutation kills
belong in the PR body per "Answer review questions with tests, not
paragraphs." A commit message that tries to be the record of everything ends
up worse at being any one of them: a `git log` reader wants what the commit
does, not the essay that led there.

Aim for what the diff needs: a one-line summary, and — only if the "why" isn't
already obvious from the code and tests — a short paragraph. If that paragraph
keeps growing, that's a sign the *change* should have been split, not that the
message needs more room.

**Comments are terse and present-tense.** A comment states a constraint the
code can't show, in a sentence or two. No novels, no archeology: how the old
implementation did it, what defect a line replaces, or which review added it
belongs in the PR body or the issue, not the source. Keep the *why* — one
line of why beats five of history.

## Review panel

Before committing anything non-trivial, run a self-review panel:

- **Four independent agents, four distinct lenses.** Typically correctness
  and control flow; data access, SQL, and resource safety; tests, docs, and
  CI gates; and altitude. The lenses should barely overlap.
- **The altitude lens re-asks the pre-code question of the finished diff.**
  Is this change at the right level, or a patch over a symptom of something
  bigger? Does each new abstraction earn its keep, and would less code do
  (see Design and simplification)? The other lenses stare at what the diff
  does; this one asks whether it should exist in this shape at all — the
  review most likely to be skipped, precisely because nothing is "wrong."
- **Panel agents read; they never write.** No edits, no `scripts/mutation-check`,
  no "revert it and see what happens" — not even a change the agent fully
  intends to undo. The panel runs several agents at once over the same files,
  so one agent's scratch mutation is another's mystery failure; an agent that
  dies mid-run leaves deliberately-broken code in the tree; and a dirty tree
  invites a commit that ships the mutation. An agent that wants to know whether
  a test bites reports that as a finding instead of finding out.
- **Enforce read-only structurally, not by instruction.** Convene panels on an
  agent type without edit or write tools, and keep write-shaped questions out
  of the briefs — "verify this test fails on the old code" is an instruction
  to mutate the tree no matter how firmly the same brief says never to. The
  rule above was already written when #1297's panel got write-capable agents
  with exactly such briefs; every predicted failure followed, including the
  mystery mutant and edits silently reverted under a live session.
- **Each agent hunts, then tries to refute its own findings** before reporting.
  This is what keeps the signal-to-noise usable.
- **Verify the survivors yourself** before acting on them. Agents are sometimes
  confidently wrong; don't take a finding at face value.
- **Aggregation is where the writing happens.** Every surviving finding not
  already covered gets a test — positive *and* negative — including the
  findings you decide *not* to act on, where the test pins the behavior you
  chose to keep so the next reader doesn't reopen the question. Mutation
  checking belongs here too: it needs a clean tree and a single writer.

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

**TDD, nearly always — not just for bug fixes.** Write the test first, watch it
fail for the right reason, then write the code. This is the default for
features as much as for fixes; the exceptions are narrow (a spike you intend to
throw away, a pure rename) and "I already know what this does" is not one of
them.

The reason is design, not discipline. Writing the expectation first forces the
question "what should this do, and how would anyone tell?" while the answer is
still cheap to change — before an interface exists to be accommodated. Tests
written afterwards answer a different question: "what does this code do?" They
inherit the shape of whatever was built, including the parts that are awkward
to observe, and they are systematically blind to the case the implementation
forgot, because they were derived from it.

The zone bug was pinned this way: a test running under `TZ=Pacific/Kiritimati`
that failed with `expected: "2026-07-01 00:00:00" but was: "2026-07-01 14:00:00"`
before the fix landed. Watching it fail is half the value — a test that has
never failed has not been shown to test anything.

**Mutation checking is not a substitute for writing the test first.** It is
worth doing (see below) and it answers a genuinely useful question, but a much
narrower one: *does this assertion, as written, bite right now?* It cannot tell
you the assertion is the right one. It cannot recover a case nobody thought to
assert, because it only mutates code that exists to break tests that exist. And
it applies long after the design decisions it might have influenced were made.

Reaching for it to justify tests written after the fact is the failure it looks
most like a fix for. Two receipts from one session:

- #1371's first regression fixture asserted the property with an absolute path,
  and passed against the very bug it was written to catch — the failure needed
  a relative path. A test that passes against the bug is worse than no test,
  and mutation checking found it only because someone thought to point the
  suite at the old script.
- #1373's first interrupt test signalled a wrapper process rather than the
  script under test. The run carried on underneath, the assertion "passed", and
  the reason had nothing to do with the behaviour being tested.

Both were written to fit code that already existed, and both looked fine.
Write the test first; mutation-check it afterwards.

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
| diff-build full-build decision | `scripts/test-diff-build` | yes — `test-deploy` |
| mutation-check harness | `scripts/test-mutation-check` | yes — `test-deploy` |

`scripts/format-all` runs the bazel/java/cc/scala formatters together. Note
what it does **not** cover: Rust, Go, and TypeScript have no formatting gate in
CI at all, despite `rustfmt.toml` and `clippy.toml` existing. Don't claim
otherwise.

New source files must be added to the relevant `srcs` in `BUILD.bazel` by name.
Two subtrees glob instead — `domains/chat/libs/yochat_lib` and
`domains/games/apps/wordchains_ios` — but everywhere else a file that isn't
listed doesn't compile and its tests don't run, while a local `go test ./...`
or `npm test` passes because neither reads `BUILD.bazel`.

**Be explicit about what couldn't be verified locally, and why.** The sandbox's
proxy 403s GitHub source archives, which is how most BCR modules fetch — boost
(so every Beast-transport target), libpng, opentelemetry-cpp, cel-spec (so
`bazel run //:buildifier`, so `scripts/format-all`). `scripts/make-git-overrides.sh`
rebuilds those modules from git clones and gets the whole graph building; run it
before concluding a target is unbuildable here. When something genuinely can't
be verified locally, prove the limitation is pre-existing by reproducing it with
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

## Writing it down

**No archeology in comments.** A comment describes the code as it is, not how
it got there. No "used to", no "previously", no retelling of the bug that
prompted the line. Git has the history, and a comment narrating a deleted
alternative ages into a lie the moment someone edits around it.

A live trap is not archeology. "`empty_body`, not a cleared `string_body`"
earns its place because clearing the body is the obvious wrong turn and the
failure is a silent hang — that warns about the code in front of you rather
than recounting a previous attempt.

**A comment must not claim a property the code doesn't have.** In #1445,
`ContentLengthOf` returned a `"<none>"` sentinel and its comment said a missing
header "reads as itself in a failure rather than as a match". The only caller
compared two of them, and two sentinels compare equal. The comment described
the guarantee the author meant to build rather than the one that shipped, which
made a vacuous assertion look deliberate. That is worse than no comment.

**Commit messages under 100 words, usually well under.** What changed and why,
in the fewest words that carry it; a one-line subject is often the whole job.
No account of how the work went and no list of what was checked — the tests are
that record.

This is the one that outlives the PR. MoonBase squash-merges with commit
details, so the branch's commit messages are concatenated into the message on
`main` — `b65cce2` inlines a single commit, `151db76` carries a `*` per commit.
A bloated commit message is bloat in `git log` forever. The PR body is not
included.

**Terse PR bodies.** The change, the consequences a reviewer cannot see from
the diff, and what is deliberately not covered. Nothing else. The body is spent
entirely on reviewer attention, which is the scarcest thing in the process.

**No journaling in any artifact.** "My first attempt", "this turned out to be",
"I then found" — none of that belongs in code, commit messages, or PR bodies.
A finding from a review lives in the review thread; the artifact carries only
the conclusion.

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
- **Never commit `MODULE.bazel.lock` churn** produced by the sandbox module
  overrides — they run under `--lockfile_mode=off` for exactly this reason.
