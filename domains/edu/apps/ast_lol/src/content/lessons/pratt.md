## When the ladder gets heavy

Expr had four precedence levels, so recursive descent's one-function-per-level ladder cost four small functions. AstQL's expression grammar has seven — `OR`, `AND`, `NOT`, comparisons (with postfix `IS NULL` at the same strength), additive, multiplicative, unary minus — and a real dialect has more (SQLite documents ten; C has fifteen). At that size the ladder is repetitive to write, and adding one operator means touching a chain of functions.

**Pratt parsing** (operator-precedence parsing) collapses the ladder into one loop and one table. It is how rust-analyzer, clox, and countless production parsers handle expressions, and it composes perfectly well with recursive descent for everything that is not an expression — which is exactly how Tier 3's statement parser will use it.

## Binding power

Give every operator a number — its **binding power**:

```js
const BP = { OR: 1, AND: 2, '=': 4, '<>': 4, '<': 4, '<=': 4, '>': 4, '>=': 4,
             '+': 5, '-': 5, '*': 6, '/': 6 };   // NOT: 3, IS: 4, unary -: 7
```

The whole parser is then:

```js
function exprBp(minBp) {
  let left = prefix();                    // literal, column, (expr), NOT …, -…
  for (;;) {
    const op = peek();
    if (!isOperator(op) || BP[op] < minBp) break;
    consume();
    const right = exprBp(BP[op] + 1);     // left-associative: right side must bind tighter
    left = { type: 'Binary', op, left, right };
  }
  return left;
}
```

Trace `a = 1 AND b = 2 OR c = 3` from `exprBp(1)` once by hand — slowly, with the table open — and the mechanism clicks: each recursive call is only allowed to consume operators that bind *at least* `minBp` tight, so tighter operators nest deeper automatically. The `+ 1` in the recursion is left-associativity, the same fact the descent ladder expressed with a loop. (For a right-associative operator you would recurse at `BP[op]` instead — AstQL has none, but now you know where the knob is.)

## Prefix and postfix join the same loop

- **Prefix operators** are handled before the loop: `NOT` parses its operand at power 4 — looser than comparison — so `NOT a = b` means `NOT (a = b)` while `NOT a AND b` means `(NOT a) AND b`. Unary minus parses its operand at 8, so `-a * b` is `(-a) * b`. Getting these two operand powers right *is* the challenge, and the test bank checks both directions.
- **Postfix operators** are handled inside the loop: on `IS`, consume optional `NOT` and required `NULL`, wrap `left` in an `IsNull` node, and continue looping — it behaves like a binary operator that brings its own right-hand side. `a + 1 IS NULL` therefore applies to the whole sum.

One more property, free of charge: the loop **stops at any token that is not an operator** — a comma, a closing paren, `FROM`, `WHERE`. It does not consume it; it just returns what it has. Keep that in mind for the next lesson, where the statement parser repeatedly says "parse an expression here" and relies on the expression parser knowing when to stop.
