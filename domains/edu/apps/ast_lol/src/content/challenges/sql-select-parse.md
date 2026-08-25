Write `parseSelect(tokens)` — the full AstQL statement parser.

Your Pratt parser is **provided** as `parseExprFrom(tokens, i) → { expr, end }` (see the provided-code panel): it parses the longest expression starting at `i` and reports the first index it did not consume. It stops on its own at commas, closing parens, and clause keywords like `FROM` or `WHERE` — operator keywords (`AND`, `OR`, `NOT`, `IS`) it consumes itself.

### Output shape

```js
{ type: 'Select',
  columns: '*' | [{ expr, alias: string|null }],
  from:    { table: string, alias: string|null },
  joins:   [{ table, alias, on }],
  where:   Expr | null,
  orderBy: [{ expr, dir: 'ASC'|'DESC' }],
  limit:   number | null }
```

### Grammar

```text
SELECT ( '*' | expr [AS ident] (',' expr [AS ident])* )
FROM ident [AS ident | ident]
( JOIN ident [AS ident | ident] ON expr )*
[ WHERE expr ]
[ ORDER BY expr [ASC|DESC] (',' expr [ASC|DESC])* ]   -- default ASC
[ LIMIT integer ]
```

- Table aliases: `AS u` or bare `u`. Column aliases: **`AS` required** — `SELECT name who …` is a syntax error at `who`.
- `LIMIT` requires an integer literal (`2.5` errors at the token).
- Absent clauses take their defaults: `joins: []`, `where: null`, `orderBy: []`, `limit: null`.
- Consume the whole stream; error on trailing tokens. Same error message conventions as always.

### Debugging notes

- The statement parser never inspects expression tokens — call `parseExprFrom`, trust `end`, and look at what comes next. If you're peeking past operators by hand, stop.
- Bare table aliases: after the table name, an `ident` token is an alias; a keyword is not. That single check handles `users u JOIN …` correctly.
- `JOIN` without `ON` should error *at the token where `ON` belonged* — the kitchen-sink test's expected pane is a good reference for every field at once.
