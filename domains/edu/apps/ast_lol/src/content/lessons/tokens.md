## From characters to lexemes

Parsing directly over characters means every rule fights whitespace, multi-character operators, and numbers at once. So every serious parser splits the job: a **tokenizer** (lexer, scanner) first groups the character stream into **tokens** — the atoms the grammar will speak in.

A token is a small record: what kind of thing it is, the text it covers, and where it started.

```js
{ kind: 'number', text: '3.25', pos: 9 }
```

For Expr, the whole vocabulary is:

| kind | examples |
|---|---|
| `number` | `3`, `3.25` — digits, optionally `.` + digits |
| `ident` | `x`, `x_1`, `_tmp` — `[A-Za-z_][A-Za-z0-9_]*` |
| `op` | `+ - * / ^` |
| `lparen`, `rparen` | `(` `)` |

Whitespace separates tokens and is otherwise discarded. It never reaches the parser — this is most of why the parser will feel clean.

## The loop

Every hand-written tokenizer is the same loop:

```js
while (i < source.length) {
  look at source[i];
  if whitespace        -> skip it
  if a digit           -> consume the whole number, emit one token
  if an ident start    -> consume the whole identifier, emit one token
  if an operator/paren -> emit it, advance one
  otherwise            -> error, with the position
}
```

Two details carry all the correctness:

**Maximal munch.** A token extends as far as it can: `123` is one number, never three. When you see a digit, loop until the digits end, *then* emit. Getting this wrong is the single most common tokenizer bug, and the grader's test bank checks it directly. (In Tier 3, the same principle decides that `<=` is one token, not `<` then `=`.)

**Positions.** Record `pos` — the index of the lexeme's first character in the *source string*. Positions feel like bookkeeping until the first time an error message says `Unexpected character '$' at 4` and points at the actual problem. Every professional tool you have used gets its lovely squiggly underlines from exactly this bookkeeping, threaded through from the tokenizer.

## Errors are output too

A tokenizer's error message is part of its contract. Expr's is pinned:

```js
throw new Error("Unexpected character '" + c + "' at " + i)
```

Deciding error behavior *up front* — which inputs are rejected, with what message — is a habit worth building now. In Tier 3 the error set grows (unterminated strings), and by Tier 4 errors become structured data rather than exceptions. The trajectory is deliberate.

One boundary case to think about before you write code: `1.` — digits followed by a dot with no digit after it. Expr says the number is `1` and the `.` is then an unexpected character. That means peeking *two* characters before consuming the dot.
