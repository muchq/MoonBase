Repair `sortRows(rows, keys)`. The starter is intentionally plausible and wrong: it applies DESC to a comparison that has already positioned nulls.

### Contract

Each key is `{ expr, dir: 'ASC' | 'DESC' }`. Use the provided `evalExpr` to evaluate it. Compare keys in order, stopping at the first difference. Nulls go **last in both directions**. Non-null values compare using JavaScript `<` and `>`. Preserve input order for ties. Return a sorted copy; do not mutate the input array or its rows.

### Predict the bug

For `[null, 2, 1]`, descending order should be `[2, 1, null]`. The starter puts null first because it flips the entire comparator. Separate the null cases from the non-null comparison before applying direction.

### Debugging notes

When both keys are null or equal, continue to the next key rather than returning immediately. When every key ties, return zero and let the stable sort preserve order. `rows.sort(...)` mutates its input; copy the array first.

### Transfer

Why must sorting happen before projection for `SELECT name FROM products ORDER BY price`? The sort needs a value that the final output drops.
