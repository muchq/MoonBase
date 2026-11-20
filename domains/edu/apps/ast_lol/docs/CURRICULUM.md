# Curriculum design

Why the course is shaped the way it is. The registry
(`src/curriculum/registry.ts`) is the source of truth for what exists; this
document records the reasoning, so a future change starts from the intent
rather than reverse-engineering it.

## Audience and stance

Experienced professional programmers with no compiler or database-internals
background. That audience changes the design in specific ways:

- **No hand-holding on programming, full rigor on the domain.** Lessons never
  explain recursion or objects; they do explain maximal munch, Kleene logic,
  and why left recursion loops, because those are the actual new material.
- **Specs are exact.** Every challenge pins node shapes, error messages, and
  ordering. Professionals debug against contracts, not vibes — and the
  deep-equal grader makes the contract the interface.
- **Soft ordering, no locks.** Steps are ordered and prerequisites are real
  (each challenge consumes the previous layer's output), but nothing is
  gated. An experienced learner who wants to jump to the optimizer can; the
  tier structure tells them what they signed up to know.

## Practice, evidence, and navigation

The current course contains 24 coding challenges and 28 optional reasoning checks
across 14 lessons. Predict, trace, repair, implement, and transfer are different
activities: a learner can execute a supplied recipe without understanding its
preconditions. Each lesson has two short checks with explanatory feedback.
New statements include worked examples and transfer questions. A passing finite
test suite is evidence, never a universal proof of a rewrite.

Tier 2 adds enter/exit traversal practice. Tier 4 separates row expression
semantics, join generation, and sorting repair before full executor integration.
Tier 5 adds a conservative boolean-or-null analysis before simplification and
an open-output counterexample search after the optimizer capstone. Its six-value
domain is explicit; failure to find a witness is not proof outside that domain.

The homepage presents shared tiers 0–3, then engine/optimizer and formatter
branches. A saved choice drives Continue and next-step links; direct links into
either branch use that branch's ordering. Nothing is locked. Effort estimates
are guidance to validate with learner observation, not measured completion times.

Visits do not complete lessons. Explicit reading and successful checks are
separate events; reference consultation is recorded separately from passing a
challenge. Legacy completion timestamps remain available and are labeled as
previous completion rather than newly demonstrated practice.

## Why two languages

**Expr** (Tiers 1–2) exists so that every foundational technique is met on a
language small enough to hold in the head: tokenization, recursive descent,
precedence/associativity, tree-walking evaluation, traversal, printing,
rewriting. Each of these ideas reappears in the SQL tiers scaled up — the
course's core pedagogical move is *same technique, higher stakes*:

| technique | met on Expr | spent on AstQL |
|---|---|---|
| tokenizing | `expr-tokenize` | `sql-tokenize` (keywords, strings, comments) |
| parsing | recursive descent ladder | Pratt + clause sequencing |
| evaluation | `expr-eval` (numbers) | `sql-execute` (rows, 3VL) |
| traversal | `expr-vars` | binding/column collection in the optimizer |
| rebuild transforms | `expr-fold` | `sql-simplify` / pushdown / prune |
| "unsafe rewrite" | fold guard rail (1/0) | 3VL-safe boolean algebra |
| printing | `expr-print` (minimal parens) | `sql-expr-print`, `sql-format` (width-aware) |

**AstQL** (Tiers 3–6) is the destination the user asked for: SQL parsing and
basic optimization. The subset is chosen so every included feature does
curricular work: joins exist because pushdown and pruning are join stories;
`NULL` exists because 3VL is *the* semantic surprise of SQL; `ORDER BY`/`LIMIT`
exist to make plan shape (Sort below Project) a real decision. `GROUP BY`,
subqueries, and outer joins are excluded — each would grow the executor and
resolver substantially. Aggregation, outer joins, and nested scopes introduce new semantic obligations; they belong in later extensions rather than the shared foundation.

## Problem selection

Each challenge is selected to force exactly one new competence, sized between
20 minutes and a few hours:

- Tier 1 — the pipeline: text → tokens → tree → value.
- Tier 2 — the tree as a value: read it (`expr-vars`), invert it
  (`expr-print`, precedence in reverse, graded by round-trip), rewrite it
  (`expr-fold`, with the finiteness guard as the first "prove your rewrite"
  moment).
- Tier 3 — scale to a real grammar: dialect tokenization, Pratt parsing (the
  binding-power table replaces the ladder), statement sequencing. The SELECT
  challenge *provides* the expression parser as a prelude, because re-pasting
  the previous solution teaches nothing.
- Tier 4 — meaning: errors-as-data resolution, mechanical planning, and the
  executor — the tier's summit, where 3VL, join order, and null-last sorting
  all bite.
- Tier 5 — the payoff: three rewrites in ascending difficulty, then the
  capstone composing them under a cost budget.
- Tier 6 — a **parallel capstone track**: the code formatter, the other
  flagship AST application. It needs only Tier 3 (a formatter never asks what
  the tree *means*), so it forks rather than stacks — take it before, after,
  or instead of Tiers 4–5. One skill-builder (`sql-expr-print`: Tier 2's
  minimal-parens printing scaled to SQL's precedence system, `NOT`/`IS NULL`
  included, plus the `--`-comment trap) and one capstone (`sql-format`: the
  greedy fit-or-break layout cascade at three scales — query, clause, chain —
  under a hard reparse-identity obligation). Prescriptive like the other skill
  challenges: a formatter's whole value is that its style is a spec, so exact
  match *is* the semantic grading here, with reparse-diagnosed failures as
  the debugging layer. It is the one capstone graded prescriptively — the
  optimizer's grader deliberately checks behavior and cost, never shape.

The capstone battery (8 queries) is chosen adversarially: seven queries each
fail some partial optimizer — cross-side conjuncts that must not move, a
contradiction whose collapse feeds pruning, `ORDER BY` columns that must
survive pruning, null-salted data so 3VL violations change results — plus one
guard query the cost model cannot reward, pinning that an optimizer does no
harm where there is nothing to win. Budgets are literals pinned to the
formula by CI, so nothing executes plans at module load.

## Grading philosophy

- **Structural grading with first-difference paths** for skill challenges:
  prescriptive specs give precise diffs (`result.joins[0].on.op`), which is
  what makes tree bugs debuggable.
- **Semantic grading for the capstone**: any well-formed plan passes if it
  matches the naive plan's rows on demo/bench databases plus empty, duplicate-bearing, null-bearing, and seeded skewed fixtures and beats the cost budget
  (reference optimizer cost × 1.25). Skills are prescriptive; the capstone is
  open — matching how the techniques are actually judged in the field.
- **The reference solution is the oracle.** Custom tests take input only; the
  displayed solution and the grading oracle are the same artifact, and CI
  proves the oracle passes every bank (`curriculum.test.ts`), that starters
  fail, and that custom-test placeholders grade green.
- **The cost model is cells, not rows** (row width × count, joins charged per
  examined pair), so *both* pushdown and pruning are visible to the meter —
  a rows-only model would make pruning unmeasurable.
- **Hints are written against observed failure modes** (the classic bug per
  concept: per-digit tokens, wrong-way associativity, null short-circuits,
  nulls-first DESC), attached per test so they arrive exactly when relevant.

## Debugging support

The failure surface is designed before the happy path: first-difference paths,
per-test console capture, runtime errors mapped to the user's line numbers
(prelude-aware), oracle-computed expectations for user-authored tests, the
reparse-explains-your-string printer checker, and the per-operator cost bill
on budget failures. The `welcome` lesson teaches the debugging loop
explicitly.

## Verification and feedback

The grader checks inputs after calls for challenges promising immutability,
including failed calls and custom tests. It reports the changed argument path.
Reference agreement alone is insufficient: tests also reject known wrong
implementations, check generated Expr round trips and folding equivalence,
exercise mixed-type identity rewrites, and check formatter idempotence at width
boundaries. Seeded databases keep failures reproducible; cost budgets still use
the fixed benchmark database.

Boolean identities have type preconditions. `TRUE AND p` may become `p` only if
`p` is proven boolean-or-null. A bare column has unknown type in the catalog.
This applies even when the logical expression is nested inside a comparison.
Both the library optimizer and revealable solution implement this contract.

Results expand the first actionable failure, expose test inputs, offer hints on
demand, and retain raw JSON alongside expandable structural views. The pipeline
lab uses the reference engine to connect source, tokens, trees, plans, results,
cost, and layout. It is an observation tool, not a grading bypass.

The browser checks (`npm run test:browser`, run in CI) cover the actual
CodeMirror editor and module worker, a nonterminating submission, persisted
drafts/progress/theme, the formatter branch, pipeline inspection, and mobile
overflow. Set `SCREENSHOT_DIR` to also save screenshots.

## Known simplifications (deliberate)

- Duplicate output column names in a Project follow JS object semantics (last
  wins); capstone queries avoid them.
- `ORDER BY` references columns, not select-list aliases.
- Comparisons chain left-associatively (`a = b = c` parses; its meaning under
  3VL is whatever the executor's rules say).
- No `GROUP BY`/subqueries/outer joins — see "Why two languages".
