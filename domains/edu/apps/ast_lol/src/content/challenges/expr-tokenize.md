Write `tokenize(source)` returning an array of Expr tokens.

### Token shape

```js
{ kind: 'number' | 'ident' | 'op' | 'lparen' | 'rparen',
  text: string,   // the lexeme, exactly as it appears in source
  pos: number }   // index of the lexeme's first character
```

### Rules

- **number** — digits, optionally followed by `.` and more digits: `3`, `3.25`. Not `.5`, and not `3.` — a dot is consumed only when a digit follows it.
- **ident** — `[A-Za-z_][A-Za-z0-9_]*`.
- **op** — one of `+ - * / ^`; **lparen**/**rparen** — `(` `)`.
- Spaces, tabs, and newlines separate tokens and are discarded.
- Anything else: `throw new Error("Unexpected character '" + c + "' at " + i)`.

### Example

```js
tokenize('12 + x_1')
// [ { kind: 'number', text: '12',  pos: 0 },
//   { kind: 'op',     text: '+',   pos: 3 },
//   { kind: 'ident',  text: 'x_1', pos: 5 } ]
```

### Debugging notes

- One token per digit is the classic first bug — consume greedily (*maximal munch*), then emit.
- `pos` indexes the **source string**, not the token list; skipped whitespace still advances it.
- Return `[]` for empty or whitespace-only input, don't throw.
- If a test fails, check the first-difference path in the failure output: `result[2].pos` tells you exactly which token, which field.
