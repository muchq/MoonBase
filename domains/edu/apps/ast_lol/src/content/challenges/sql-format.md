The capstone of the formatter track. Write `format(select, width)` — render a parsed Select in the canonical style, flat when it fits, broken where the style says when it doesn't.

`printExpr` (your previous challenge) is **provided** — this challenge is about clause rendering and layout, not expressions.

### The flat form

One line, used whenever its length is ≤ `width`:

```sql
SELECT u.name AS who, o.total FROM users AS u JOIN orders AS o ON u.id = o.user_id WHERE ... ORDER BY o.total DESC, u.name LIMIT 10
```

- `AS` for **both** column and table aliases (bare-alias input still prints with `AS`).
- `ORDER BY`: `expr DESC`, or bare expr — `ASC` is the default and is omitted.
- `SELECT *` renders as `*`.

### The broken form

When the flat form exceeds `width`, every clause starts its own line, and each clause decides again at its own scale:

- **SELECT / ORDER BY lists**: the whole clause inline if it fits; else the keyword alone on its line, then one item per line indented 2 spaces, **trailing commas** on all but the last.
- **WHERE / JOIN … ON predicates**: inline if the clause line fits; else split the **top-level chain of the loosest `AND`/`OR`**: the first operand stays on the clause line (`WHERE c1` / `JOIN t AS a ON c1`), each remaining operand on its own line indented 2, **operator leading** (`  AND c2`). Nested groups — an `AND` inside an `OR` chain, or a parenthesized group — stay inline and move as units, and a split operand **keeps the parens its position requires** (first operand: the chain operator's level; the rest: one tighter), so the lines always rejoin to the identical tree.
- **Unavoidable overflow**: expressions never wrap internally, so any line holding one unsplittable unit — a whole over-wide predicate, a chain operand after its head or operator, a list item — may exceed the width.
- `FROM` and `LIMIT` are always their own single lines; `SELECT *` never breaks. Join lines with `'\n'`.

### Grading

Exact match against the reference formatter across a battery of queries and widths. Failures are diagnosed in layers: does your output **reparse to the same query** (fix rendering first)? If yes, which **line** first differs — and is your line over the width?

### Debugging notes

- Build `flat()` first and get the canonical-style tests green before touching layout — layout bugs on top of rendering bugs are unreadable.
- The two helpers that carry the whole thing: `list(head, items)` and `predicate(head, expr)`, each doing its own fits-inline check against `width`.
- Chain splitting flattens only the *same* operator as the root, left-associated: `chainOf(expr, expr.op)`. Splitting mixed operators changes meaning.
- Add custom tests with your own queries at several widths — watching one query pass through 300 → 60 → 28 is the fastest way to internalize the cascade.
