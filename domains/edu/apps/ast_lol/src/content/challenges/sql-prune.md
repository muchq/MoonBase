Write `prune(plan)` — cut unused columns at the base of joins, so the nested loop merges narrow rows.

Provided helpers: `exprRefs(expr)` (→ `Set` of `'binding.column'`), plus `exprBindings` / `planBindings`.

### The rewrite

1. **Collect the needed set**: every `'binding.column'` referenced by *any* expression anywhere in the plan — Project columns, Filter predicates, Join `ON`s, Sort keys.
2. **Insert pruning Projects**: above each `Scan` that sits under at least one `Join`, if some of its columns are unneeded:

```js
{ type: 'Project', input: scan,
  columns: [ /* for each needed column of this binding, in Scan column order: */
    { expr: { type: 'Column', table: binding, name },
      name: binding + '.' + name } ] }
```

Qualified output names (`'u.id'`) — plan-internal rows keep plan-internal keys, so everything above keeps evaluating unchanged.

3. **Skip the no-ops**: a Scan needing every column gets no Project (it would only add cost), and Scans outside any Join are left alone entirely.

Rebuild, never mutate.

### Debugging notes

- If validation fails with "references 'o.total', which its input does not produce", your needed-set walk missed a clause — Sort keys and Join `ON`s count.
- Column order inside the pruning Project follows the **Scan's** column list, not discovery order.
- `SELECT *` needs everything — the correct output is the unchanged plan. Same for single-table queries: "under a Join" is a hard condition.
- The cost test at the end proves the point on the bench database: same rows, fewer cells per examined pair.
