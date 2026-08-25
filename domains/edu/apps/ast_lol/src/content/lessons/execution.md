## Rows through operators

An executor makes the plan's dataflow literal. In this course each operator is a function from arrays of rows to arrays of rows, applied bottom-up — the tree-walking interpreter from Tier 1, with rows for numbers. (Real engines stream row-at-a-time through iterators — the *Volcano* model — or vectorize into batches; the semantics you implement here are identical, which is why the simple form is the right one to learn on.)

- **Scan** reads `db[table]` and emits rows with binding-qualified keys: `{ 'u.id': 1, 'u.name': 'ada', … }`. A column missing from a stored row reads as `NULL`.
- **Join** is the honest nested loop: for each left row, for each right row, merge and test `ON`; keep pairs where it is exactly `true`. Row order — left order, then right order — is part of the contract your tests pin.
- **Filter** keeps rows where the predicate is exactly `true`. "Exactly" is about to matter.
- **Sort** is stable, multi-key. **Limit** slices. **Project** evaluates each output expression per row.

## NULL changes the logic — all of it

`NULL` means *unknown*, and unknown is contagious. SQL's expression logic is **three-valued** (Kleene logic): every predicate evaluates to `true`, `false`, or `null`, and AstQL implements it in full:

| expression | result | because |
|---|---|---|
| `city = 'london'` (city NULL) | `null` | unknown = anything is unknown |
| `city <> 'london'` (city NULL) | `null` | still unknown — `<>` is not a loophole |
| `null AND false` | **`false`** | false wins regardless of the unknown |
| `null AND true` | `null` | outcome hinges on the unknown |
| `null OR true` | `true` | true wins regardless |
| `NOT null` | `null` | negating unknown is unknown |
| `total + null` | `null` | arithmetic is strict |
| `x / 0` | `null` | AstQL's choice — SQL flavor, unlike Expr's `Infinity` |
| `city IS NULL` | `true`/`false` | the *only* operators that answer definitely |

One more rule keeps the algebra honest: **`AND`/`OR`/`NOT` coerce each operand first** — `true` stays `true`, `null` stays `null`, and anything else counts as `false` — and they always return `true`, `false`, or `null`. The point is *consistency*: a value must not read as "not false" in one branch and "not true" in another, or Tier 5's `TRUE AND p → p` rewrite would quietly change answers for ill-typed predicates. (Real SQL dialects dodge this with a type checker that rejects `WHERE 5 AND x` outright; AstQL, having none, defines the case instead. Feeding a logical result into *arithmetic* stays outside the contract.)

Then one rule turns logic into rows: **Filter and Join keep a row only when the predicate is exactly `true`.** `null` is dropped like `false`. This is why `WHERE city = 'london'` and `WHERE city <> 'london'` can *both* omit the person with an unrecorded city — the single most-reported "SQL bug" that isn't one.

Two implementation warnings, both covered by tests with hints: `null AND false` must be `false`, so a lazy short-circuit that returns `null` on any null operand is wrong. And sorting: **nulls order last regardless of direction** — implement `DESC` by flipping the comparison of *non-null* keys, not by negating a comparator that has already placed nulls.

## Counting the work

The executor also defines this course's **cost model**, which Tier 5 grades against: every operator is charged the number of **cells** (row-width × rows) that enter it, and the nested-loop Join is charged for **every pair it examines** — kept or not. Total cost = the sum over all operators.

That model makes the optimizer's opportunities concrete before you write it. A filter *above* a join pays for the full cross-product and then discards; the same filter pushed *below* shrinks one side of every pair the loop will ever examine. Narrow rows make each pair cheaper — that is column pruning. When your capstone plan comes in under budget, this is the meter it is beating; when it does not, the grader prints each operator's bill so you can see exactly where the cells went.
