Write `resolve(select, catalog)` — semantic analysis: check every name, annotate the tree.

`catalog` maps table names to column lists: `{ users: ['id','name','city','signup_year'], … }`. Return `{ select, errors }`.

### Phase 1 — bindings (FROM, then JOINs in order)

Binding name = alias if present, else table name.

- Table not in catalog → `{ kind: 'unknown-table', name: table }` (contributes no binding).
- Binding name already taken → `{ kind: 'duplicate-binding', name: binding }`.
- **Any phase-1 error**: return `{ select: null, errors }` — columns are not checked.

### Phase 2 — columns, in clause order

SELECT list → each JOIN's `ON` → WHERE → ORDER BY. Collect, don't throw:

- Qualified `q.c`: binding `q` must exist and own `c` → else `{ kind: 'unknown-column', name: 'q.c' }`.
- Bare `c`: exactly one binding owns it. None → `{ kind: 'unknown-column', name: 'c' }`; several → `{ kind: 'ambiguous-column', name: 'c' }`.

Errors found → `{ select: null, errors }` in discovery order.

### Success — the annotated tree

Return a **new** Select (never mutate the input) where:

- every `Column.table` is filled with its binding name;
- `'*'` is expanded to every binding's columns, in binding order then catalog column order, each as `{ expr: Column, alias: null }`.

### Debugging notes

- Aliasing *replaces* the table name as a binding: after `users u`, `users.id` is an unknown column. There's a test for it.
- The error-order test fails if you check WHERE before the JOIN `ON`s — walk clauses in the specified order.
- This is the rebuild-traversal from Tier 2 plus a `Map` of bindings; if it's getting long, you're probably duplicating the expression walk per clause instead of writing it once.
