## Statements are sequencing

After Pratt parsing, statement parsing is almost restful. A `SELECT` statement is a fixed sequence of clauses, each introduced by a keyword; there is no precedence to negotiate, just order:

```text
SELECT columns FROM tableRef (JOIN tableRef ON expr)*
  (WHERE expr)? (ORDER BY key (',' key)*)? (LIMIT int)?
```

The parser reads like the grammar: expect `SELECT`, parse the column list, expect `FROM`, parse a table reference, loop while the next keyword is `JOIN`, and so on. Each optional clause is an `if (atKeyword(…))`; each repeatable one is a loop with a comma or keyword test. This is recursive descent again, in its natural habitat — the Pratt parser handles the expression *islands* inside the clause structure.

The provided `parseExprFrom(tokens, i) → { expr, end }` is your own Tier 3 expression parser, repackaged to start mid-stream and report where it stopped. Because a Pratt loop halts at any non-operator token, `WHERE u.city = 'x' ORDER BY …` just works: the expression parser consumes through `'x'`, returns, and the statement parser finds `ORDER` waiting. No lookahead negotiation, no backtracking. This clean handoff is the design payoff of the previous lesson.

## The decisions in the details

Small grammar questions with real answers — AstQL pins each one, and the test bank enforces them:

- **Aliases.** Table references take an alias with `AS` or bare (`users u` — universal SQL idiom). Select-list expressions take an alias with `AS` **only**: after an expression, a bare identifier has no comma or keyword protecting it, and permitting it costs real ambiguity budget. (Real dialects do allow it; they pay for it elsewhere.)
- **`*` or a list** — `SELECT *` is a distinct shape (`columns: '*'`), not a magic column. Expanding it into actual columns requires knowing the table's schema, which the parser does not have. That is Tier 4's first job.
- **`LIMIT` takes an integer literal.** `LIMIT 2.5` is a syntax error. Cheap to enforce in the parser; costly to discover anywhere later.
- **Consume everything.** After `LIMIT`'s number (or whatever the last clause was), the token stream must be empty. Trailing-garbage errors (`SELECT * FROM t 1`) are among the most valuable a parser produces — without the check, typos silently truncate queries.

Error reporting stays in the style you built in Tier 1: unexpected token with its position, unexpected end of input when the stream runs dry. A `JOIN` missing its `ON` should point at the token found where `ON` belonged. You have all the machinery; this challenge is about wiring it with care.

After this challenge, text is fully behind you: `"SELECT …"` is now a `Select` tree, and the rest of the course never looks at a character again.
