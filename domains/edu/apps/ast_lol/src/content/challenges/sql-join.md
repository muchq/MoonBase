Write `joinRows(left, right, on)`. Inputs are arrays of rows with disjoint, binding-qualified keys; `on` is an AstQL expression. The provided `evalExpr(expr, row)` has the expression semantics from the preceding milestone.

### Contract

Visit left rows in input order. For each left row, visit every right row in input order. Merge them into a new row and keep it only when `evalExpr(on, merged)` is **exactly true**. Preserve duplicates and leave both input arrays and their rows unchanged.

### Trace a pair

A left row `{ 'u.id': 2 }` and right row `{ 'o.user_id': 2 }` merge to `{ 'u.id': 2, 'o.user_id': 2 }`. The predicate `u.id = o.user_id` keeps that row. Two matching right rows produce two output rows; SQL joins do not deduplicate.

### Debugging notes

An empty input on either side produces no rows. `NULL = NULL` is null and must not match. Do not filter with JavaScript truthiness, and do not merge into a source row with `Object.assign(leftRow, rightRow)`.

### Transfer

For 100 left rows and 600 right rows, this implementation examines 60,000 pairs. Predict the pair count if a filter reduces the right side to 20 rows. That reduction is the motivation for pushdown.
