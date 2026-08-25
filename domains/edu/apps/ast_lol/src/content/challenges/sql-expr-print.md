Write `printExpr(expr)` — canonical flat text for an AstQL expression, with **minimal parentheses**.

### Style

- Keywords uppercase: `AND OR NOT IS NULL TRUE FALSE`.
- Single spaces around binary operators; unary minus tight; `t.name` for qualified columns.
- Strings in single quotes with `'` doubled: the value `o'brien` prints as `'o''brien'`.
- Numbers via `String(value)`; `NULL`/`TRUE`/`FALSE` as keywords.

### The uniform paren rule

Wrap a child in parens exactly when its precedence is **below what its position requires**:

| level | forms |
|---|---|
| 1 | `OR` |
| 2 | `AND` |
| 3 | `NOT` |
| 4 | comparisons, `IS [NOT] NULL` |
| 5 | `+ -` |
| 6 | `* /` |
| 7 | unary `-` |
| 8 | atoms (columns, literals) |

Positions: a binary operator at level *p* requires *p* on the left and *p + 1* on the right (everything associates left). `NOT`'s operand requires 4 (it parses at comparison strength). `IS [NOT] NULL`'s operand requires 4. Unary `-`'s operand requires 8.

Consequences the tests check: `NOT (a AND b)` but bare `NOT a = 1`; `(a AND b) IS NULL` but bare `a + 1 IS NULL`; `a = (b IS NULL)`; `a - (b - c)`; and `-(-a)` keeps its parens — which also dodges bare `--` lexing as a comment.

### Grading

Exact string match — but a wrong answer is diagnosed by **reparsing your output**: "different tree" means parens are missing or extra; "right tree, style off" means spacing, casing, or over-parenthesization.

### Debugging notes

- This is Tier 2's printer with a bigger table: one `prec(node)` helper, one `wrap(child, min)` helper, one switch.
- If a paren case surprises you, parse the bare form in a custom test and look at which tree it builds.
