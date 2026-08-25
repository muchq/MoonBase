## A SQL worth parsing

The skills from Expr now meet a language people actually ship. **AstQL** is this course's SQL subset — small enough to implement honestly in a browser, real enough that Tier 5's optimizer will have something worth optimizing:

```sql
SELECT u.name AS who, o.total
FROM users u
JOIN orders o ON u.id = o.user_id
WHERE o.year >= 2024 AND u.city <> 'boston'   -- inner joins only
ORDER BY o.total DESC, u.name
LIMIT 10
```

What is in: `SELECT` lists (with `*` and `AS` aliases), one `FROM` table, any number of inner `JOIN … ON`s, `WHERE`, multi-key `ORDER BY` with `ASC`/`DESC`, `LIMIT`, and a full expression language — comparisons, arithmetic, `AND`/`OR`/`NOT`, `IS [NOT] NULL`, literals including `NULL`, `TRUE`, `FALSE`. What is out: `GROUP BY`, subqueries, outer joins, `DISTINCT`, functions. (Each of those is a fine exercise *after* this course; none changes the techniques you are here to learn.)

## Tokenizing a real dialect

Expr's tokenizer grows four ideas, each a genuine SQL behavior:

- **Keywords vs. identifiers, case-insensitively.** `select`, `Select`, and `SELECT` are the same keyword; `Name` and `name` are the same column. AstQL canonicalizes at the tokenizer: keywords uppercase, identifiers lowercase. There is exactly one keyword list; scan the word first, then classify. (This is also why keywords can never be column names — a simplification real dialects sweat over with quoted identifiers.)
- **String literals with escapes.** `'o''brien'` is the string `o'brien` — SQL escapes a quote by doubling it. The token keeps the raw lexeme in `text` and the decoded contents in `value`, because downstream stages want the value while error messages want the spelling. Unterminated strings are the new error case: report where the string *started*, which is where the user's eye needs to go.
- **Two-character operators.** `<=`, `>=`, `<>` — maximal munch again, now with lookahead: try two characters before one.
- **Comments.** `--` to end of line, discarded like whitespace. The subtle case: `1 - -2` is two minus operators, `1 --2` is a comment. One character of lookahead decides.

## The AST you are building toward

Expressions:

```js
{ type: 'Column', table: 'u' | null, name: 'city' }
{ type: 'Lit', value: 42 | 'text' | true | null }
{ type: 'Binary', op: 'AND'|'OR'|'='|'<>'|'<'|'<='|'>'|'>='|'+'|'-'|'*'|'/', left, right }
{ type: 'Not', operand }
{ type: 'Unary', op: '-', operand }
{ type: 'IsNull', operand, negated: boolean }
```

Statements:

```js
{ type: 'Select',
  columns: '*' | [{ expr, alias: string|null }],
  from:    { table, alias: string|null },
  joins:   [{ table, alias, on }],
  where:   Expr | null,
  orderBy: [{ expr, dir: 'ASC'|'DESC' }],
  limit:   number | null }
```

Note the recurring shape decision: **absent clauses are represented, not omitted** — `where: null`, `joins: []`. Consumers (the resolver, the planner) get one shape to handle instead of a field-existence check per clause. When you design your own ASTs, this choice is yours to make; make it once, on purpose.

The three challenges in this tier build the tokenizer, the expression parser, and the statement parser — in that order, each consuming the previous one's output.
