Write `pushDown(plan)` — move filters below the joins they don't need to be above.

Provided helpers (panel above the editor): `exprBindings(expr)`, `planBindings(plan)`, `exprRefs(expr)`.

### The rewrite

For every `Filter` whose input is a `Join`:

1. **Split** the predicate into AND-conjuncts — flatten the left-leaning `AND` tree, left to right. `OR` never splits: a disjunction is one conjunct.
2. **Classify** each conjunct by `exprBindings(conjunct)`:
   - every binding in `planBindings(join.left)` → push to the **left**;
   - every binding in `planBindings(join.right)` → push to the **right**;
   - spans both sides, or references **no bindings at all** → stays at the join.
3. **Rebuild**: pushed conjuncts become one `Filter` directly above their side (re-joined with `AND` in original order); staying conjuncts re-form a `Filter` above the join, or the Filter disappears if none remain.
4. **Recurse** — a pushed Filter may now sit on a lower Join and split again. That cascade is what moves `u.city = 'x'` down through a three-table stack.

Filters not sitting on a Join (and every other node) just recurse into their inputs. Rebuild, never mutate.

### Why constants stay

An empty binding set is vacuously a subset of both sides — classify "no bindings" explicitly as staying. Deciding what `1 = 1` *means* is simplification's job; pushdown only moves work that provably belongs to one side.

### Debugging notes

- Most failures are classification: log `[...exprBindings(c)]` for each conjunct against the two `planBindings` sets.
- The cascade test fails if you rebuild the join but forget to recurse into its (new) children.
- The final test executes your plan on the bench database: if rows differ, you moved a conjunct that references both sides; if cost didn't drop, nothing actually moved.
