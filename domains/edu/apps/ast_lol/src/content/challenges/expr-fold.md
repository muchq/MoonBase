Write `fold(ast)` — constant folding: replace computation-on-constants with its result.

### Contract

- Bottom-up: fold children first, then fold this node if its (already-folded) children allow it.
- `Binary` with two `Num` children → a `Num` holding the result (same operator semantics as your evaluator). `Unary` over a `Num` → a negated `Num`.
- **Guard rail**: fold only when the result satisfies `Number.isFinite`. `1 / 0` and `0 / 0` stay as division nodes — Expr has no literal that could round-trip `Infinity` or `NaN`.
- Variables (and anything containing them) stay put: `x + 2 * 3` → `x + 6`.
- Never mutate the input; rebuild changed nodes.

### Examples

```js
fold(parse('1 + 2 * 3'))    // { type: 'Num', value: 7 }
fold(parse('x + 2 * 3'))    // Binary('+', Var x, Num 6)
fold(parse('1 / 0'))        // unchanged Binary('/')
```

### Debugging notes

- If `1 + 2 * 3` comes back unfolded at the top: you folded the children but tested `ast.left`/`ast.right` (the originals) instead of the folded results.
- If only one level folds: you forgot to recurse before checking.
- The guard rail test exists because "always fold" *feels* correct — the lesson on unsafe rewrites explains why it isn't.
