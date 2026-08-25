Write `buildPlan(select, catalog)` — lower a **resolved** Select into the canonical logical plan.

The input is post-resolution: `*` already expanded, every `Column.table` a binding.

### Plan nodes

```js
{ type: 'Scan', table, binding, columns }            // columns = catalog[table]
{ type: 'Join', left, right, on }
{ type: 'Filter', input, predicate }
{ type: 'Sort', input, keys }                        // keys = orderBy entries as-is
{ type: 'Project', input, columns: [{ expr, name }] }
{ type: 'Limit', input, count }
```

### Canonical stack, bottom to top

1. `Scan` for FROM (binding = alias ?? table).
2. One `Join` per JOIN clause, **left-deep in source order**: the second join's `left` is the first join.
3. `Filter` if WHERE is present.
4. `Sort` if ORDER BY is present — **below** Project, so keys may use dropped columns.
5. `Project`, always. Output `name`: the alias if given; else a `Column`'s own name; else `'col' + (index + 1)` by SELECT-list position.
6. `Limit` if present.

Absent clauses contribute no node.

### Debugging notes

- This is intentionally mechanical — resist optimizing anything here. The optimizer tier depends on planners being dumb and predictable.
- The `colN` counter is positional: `SELECT price * 2, price AS doubled` names them `col1`, `doubled` — not `col1`, `col2`.
- Output names may collide (`SELECT u.id, o.id …`, or `*` over a join where every table has `id`). The planner does not deduplicate: rows are objects, so at execution the **last** column with a name wins. A deliberate v1 simplification — alias with `AS` to keep both.
- If the join test fails, check nesting direction: `Join(Join(Scan users, Scan orders), Scan products)`, never the mirror.
