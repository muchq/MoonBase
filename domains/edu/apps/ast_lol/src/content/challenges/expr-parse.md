Write `parse(tokens)` — a recursive-descent parser from Expr tokens to an AST.

### AST shape

```js
{ type: 'Num', value: number }      // Number(token.text)
{ type: 'Var', name: string }
{ type: 'Unary', op: '-', operand: Expr }
{ type: 'Binary', op: '+'|'-'|'*'|'/'|'^', left: Expr, right: Expr }
```

### Precedence, loosest → tightest

| level | operators | associativity |
|---|---|---|
| 1 | `+` `-` | left |
| 2 | `*` `/` | left |
| 3 | unary `-` | — |
| 4 | `^` | **right** |

Consequences the tests check: `-2 ^ 2` is `-(2 ^ 2)` (unary looser than `^`), `2 ^ -3` is legal (`^`'s right operand re-enters the unary level), `1 - 2 - 3` leans left, `2 ^ 3 ^ 2` leans right.

### Errors

- Unexpected token: `throw new Error("Unexpected token '" + t.text + "' at " + t.pos)`
- Stream ends too soon: `throw new Error('Unexpected end of input')`
- Consume the **whole** stream — `1 2` is an error at the `2`.

### Debugging notes

- One function per precedence level; **loop for left-associative levels, recurse for the right side of `^`**. A right-leaning `1 - 2 - 3` means you recursed where you should have looped.
- Parens are handled in the atom rule: consume `(`, parse a full expression, require `)` — they appear nowhere in the AST.
- `console.log(JSON.stringify(tree, null, 2))` inside a failing case, and compare against the expected pane — the first-difference path names the exact node that differs.
