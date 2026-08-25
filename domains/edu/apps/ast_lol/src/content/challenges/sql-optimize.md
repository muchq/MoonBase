The capstone. Write `optimize(plan)` — produce **any** well-formed plan that:

1. returns **exactly** the same rows as the input plan (checked by executing both on the demo *and* bench databases, row for row, order included), and
2. fits each test's **cost budget** on the bench database — cells through operators, as the execution lesson defined. Budgets are set 25% above the reference pipeline: room for a different-but-honest optimizer, none for skipping a technique.

### The expected shape (yours to deviate from)

```text
simplify predicates  →  push filters down  →  prune columns
```

Your three previous solutions compose directly — paste them in and wire them together. Order matters: simplify first so pushdown sees clean conjuncts; prune last so it counts the pushed filters' column uses. This grader checks **behavior and cost, not structure** — beat the budget any semantically-honest way you like.

### The battery

Eight queries. Seven are budgeted below their naive cost: the classic two-sided pushdown, a three-table cascade, a cross-side conjunct that must stay put, a `WHERE 1 = 2` whose collapse lets pruning cut deeper, `ORDER BY`/`LIMIT` interplay, and `IS NOT NULL` predicates over data salted with nulls. The first is a **guard**: a single-table plan the cost model cannot reward — your optimizer must return an equivalent plan without making it *worse*. Each test's hint names the technique it's probing.

### Debugging notes

- **Equivalence failures**: the message names the database and the first differing row path. Wrong rows after pushdown almost always mean a cross-side conjunct moved; test q4 exists for exactly this.
- **Budget failures** print your plan's per-operator bill. Read it like a profiler — one Join dwarfing everything means its inputs are unfiltered (pushdown not reaching) or unpruned (wide rows).
- Add your own queries as custom tests — anything against the shop catalog gets budgeted against the reference optimizer automatically.
- Timeout is 10s; if you hit it, look for a rewrite loop that re-wraps the same Filter forever.
