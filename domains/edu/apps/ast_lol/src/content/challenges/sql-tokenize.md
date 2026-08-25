Write `tokenizeSql(source)` — the AstQL tokenizer.

### Token shape

```js
{ kind: 'keyword'|'ident'|'number'|'string'|'op'|'punct',
  text: string, pos: number,
  value?: string }   // strings only: the decoded contents
```

### Rules

- **Keywords**, matched case-insensitively, canonicalized to uppercase `text`: `SELECT FROM WHERE AND OR NOT AS ORDER BY ASC DESC LIMIT JOIN ON IS NULL TRUE FALSE`. Every other word is an **ident**, folded to lowercase.
- **Strings**: `'…'`, with `''` as an escaped quote. `text` keeps the raw lexeme (quotes and all); `value` carries the decoded contents: `'it''s'` → `text: "'it''s'"`, `value: "it's"`. Unterminated: `throw new Error('Unterminated string starting at ' + start)`.
- **Numbers**: digits, optionally `.` + digits (as in Expr).
- **Ops**: `= <> < <= > >= + - * /` — maximal munch: try `<>`, `<=`, `>=` before single characters.
- **Punct**: `,` `(` `)` `.`
- **Comments**: `--` to end of line, discarded. `1 - -2` is two minus ops; `1 --2` is a comment.
- Whitespace separates; anything else: `throw new Error("Unexpected character '" + c + "' at " + i)`.

### Debugging notes

- Order the checks: whitespace → comment (`-` **and** next is `-`) → number → word → string → two-char ops → one-char ops → punct → error.
- Classify words *after* scanning them: uppercase the word, check the keyword set.
- `SELECT *` — the star is an ordinary `op` token here; the parser decides what it means.
