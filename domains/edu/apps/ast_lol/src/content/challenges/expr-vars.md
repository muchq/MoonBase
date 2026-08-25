Write `collectVars(ast)` returning every distinct variable name in the tree, sorted ascending.

### Contract

- Every `Var` node's `name`, at any depth.
- No duplicates: `x * x + x` → `['x']`.
- Sorted with default string ordering: `['alpha', 'mid', 'zeta']`.
- A constant expression returns `[]`.

### Debugging notes

- This is the traversal skeleton you'll reuse all course: one case per node type, recursing into every child field. If a name is missing from your result, some case isn't recursing into one of its children — `Binary` has **two**.
- Collect into a `Set` (or object) as you walk; convert and sort once at the end.
