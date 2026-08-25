Write `parseExpr(tokens)` — a Pratt parser for AstQL expressions.

### AST shape

```js
{ type: 'Column', table: string|null, name: string }   // u.id / name
{ type: 'Lit', value: number|string|boolean|null }
{ type: 'Binary', op: 'AND'|'OR'|'='|'<>'|'<'|'<='|'>'|'>='|'+'|'-'|'*'|'/', left, right }
{ type: 'Not', operand }
{ type: 'Unary', op: '-', operand }
{ type: 'IsNull', operand, negated: boolean }
```

### Binding powers (all binary ops left-associative)

| power | operators |
|---|---|
| 1 | `OR` |
| 2 | `AND` |
| 3 | `NOT` (prefix — operand parses at power 4) |
| 4 | `= <> < <= > >=`, and postfix `IS [NOT] NULL` |
| 5 | `+ -` |
| 6 | `* /` |
| 7 | unary `-` (prefix — operand parses at power 8) |

So `NOT a = b` is `Not(a = b)` but `NOT a AND b` is `And(Not(a), b)`; `a + 1 IS NULL` wraps the whole sum; `-a * b` is `Mul(Unary(a), b)`.

### Atoms

Number and string literals (use the token's `value` for strings), `NULL`/`TRUE`/`FALSE` → `Lit`, `ident` or `ident.ident` → `Column`, `( expr )`.

### Errors

Same conventions as Expr: unexpected token with text and position; `Unexpected end of input`; consume the whole stream. `a IS 1` errors at the `1` (after optional `NOT`, `NULL` is required).

### Debugging notes

- The whole parser is `exprBp(minBp)`: parse a prefix, then loop while the next operator's power ≥ `minBp`, recursing at `power + 1` for the right side.
- Handle `IS` **inside** the loop like a binary operator that brings its own right-hand side.
- If your precedence is scrambled, log the tree for `NOT a = 1 AND b = 2 OR c = 3` and compare shapes level by level against the expected pane.
