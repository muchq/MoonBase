## Rewrites that pay rent

Everything converges here. You can parse a query, check it, plan it, and run it. An **optimizer** is "just" more of what Tier 2 taught — meaning-preserving tree rewrites — except now the trees are plans, the meaning is checked by *executing both versions*, and every rewrite has to justify itself against a meter.

Two obligations, in strict priority order:

1. **Equivalence.** The rewritten plan returns exactly the same rows. Not similar, not "same set, different order" — in this course, identical arrays. A fast wrong answer is a bug with good latency.
2. **Improvement.** Fewer cells through the operators, as defined by the executor's cost model. The capstone gives each query a hard budget derived from the reference optimizer.

The production version of this design is everywhere: SQLite's optimizer overview reads as a catalog of such rewrites; Calcite ships them as composable rules; Postgres's planner spends thousands of lines deciding when they apply. Yours will be three rules deep — the three that matter most.

## The three rules

**Simplify predicates** (constant folding, Tier 2's move, under 3VL). `TRUE AND p` is `p`; `FALSE AND p` is `FALSE` — even when `p` might be `null`, because Kleene says false absorbs. Strict operators fold a literal `NULL` straight through: `total + NULL > 100` is `null` for every row, no data needed. And a Filter whose predicate simplified to `TRUE` disappears entirely. The 3VL caution is the lesson: `p AND p`-style "obvious" boolean algebra must be re-proven under three values before you may use it.

**Push filters down.** The big one. A Filter above a Join pays nested-loop prices for rows it was always going to discard. Split the predicate on `AND` into conjuncts; each conjunct that mentions columns from only one side of the join moves directly above that side. Conjuncts spanning both sides stay at the join. Applied recursively, a filter cascades down a stack of joins to land just above the scans — where the executor's pair-counting makes it worth the most. The classification tool is exactly the traversal you have written five times: which bindings does this expression reference?

**Prune columns.** Rows are as expensive as they are wide. Collect every `binding.column` the plan references above the scans; above each scan that feeds a join, insert a Project keeping only that scan's needed columns. The nested loop now merges narrow rows, and every pair costs less. (The pruning Projects keep qualified names like `u.id` — plan-internal rows keep plan-internal keys.)

Order matters and is worth reasoning through, not memorizing: simplify first (so pushdown sees clean conjuncts), prune last (so it counts the pushed filters' columns among the needed).

## Keeping yourself honest

How do you *know* a rewrite preserved meaning? In this course you don't argue — you measure. The graders run your plan and the naive plan on databases you have never tuned against (including a 600-order bench set with `NULL`s salted in) and compare row for row; then they read the meter. The capstone accepts **any** well-formed plan that matches results and fits the budget: match the reference pipeline or invent something better; the grader checks behavior, not shape.

When a cost test fails, it prints each operator's bill. Read it like a profiler: the number that dwarfs the others is a Join processing wide or unfiltered inputs, and it names which rule you have not applied yet. This loop — rewrite, execute, diff, re-measure — is the actual daily practice of people who work on optimizers; the tooling here is small, but the discipline is the real thing.
