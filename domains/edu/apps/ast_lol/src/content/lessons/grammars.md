## The shape of an expression

`1 + 2 * 3` is 7, not 9, because multiplication binds tighter than addition. `1 - 2 - 3` is -4, not 2, because subtraction associates left. Everyone knows these rules; the question is where they *live* in a parser. The answer: in the shape of the grammar.

Here is Expr's grammar, written so that precedence and associativity fall out mechanically:

```text
additive        := multiplicative ( ('+' | '-') multiplicative )*
multiplicative  := unary ( ('*' | '/') unary )*
unary           := '-' unary | power
power           := atom ( '^' unary )?
atom            := number | ident | '(' additive ')'
```

Each rule is one *precedence level*, and each rule only ever calls the next-tighter level. `additive` never sees a `*` — by the time control returns to it, the whole multiplicative subtree is already built. That is the entire trick.

## Recursive descent

**Recursive descent** turns that grammar into code one-for-one: each rule becomes a function; sequencing becomes statements; alternation becomes an `if` on the next token; repetition becomes a loop. It is the technique behind many production parsers you use daily — famously, most C/C++ front ends and V8's parser are hand-written recursive descent — because it is fast, debuggable, and gives you complete control over error messages.

The pattern for a left-associative level:

```js
function additive() {
  let left = multiplicative();
  while (nextIs('+') || nextIs('-')) {
    const op = consume().text;
    left = { type: 'Binary', op, left, right: multiplicative() };
  }
  return left;
}
```

Read the loop carefully: it *folds into `left`*. After `1 - 2 - 3` it has built `(1 - 2) - 3`. If you had instead recursed — `right: additive()` — you would get `1 - (2 - 3)`: the tree leans the wrong way and subtraction silently becomes wrong. **Loop for left, recurse for right** is the mantra; `power` recursing on its right side is exactly how `^` leans right.

Two Expr-specific wrinkles, both decided by the grammar above:

- **Unary minus sits between `* /` and `^`.** So `-2 ^ 2` is `-(2 ^ 2)` — the mathematical convention — while `-a * b` is `(-a) * b`. You do not need to remember this while coding; you need the `unary` and `power` functions to call each other exactly as the grammar says.
- **`^`'s right operand re-enters `unary`.** That is what lets `2 ^ -3` parse without parentheses.

## Why not left recursion?

The "natural" grammar rule `additive := additive '+' multiplicative` is *left-recursive*: the function's first act would be to call itself with nothing consumed — infinite regress. The loop form above is the standard mechanical fix. When you read grammar-driven tooling (ANTLR, tree-sitter) you will see the same transformation performed for you; writing it by hand once makes those tools legible.

## The AST is the output

The parser's product is the tree, and the tree's shape is a **contract** — in this course, literally: the grader deep-compares your nodes field by field.

```js
{ type: 'Num', value: number }
{ type: 'Var', name: string }
{ type: 'Unary', op: '-', operand: Expr }
{ type: 'Binary', op: '+'|'-'|'*'|'/'|'^', left: Expr, right: Expr }
```

Note what is *not* here: no parentheses (they only steered the build), no positions (real compilers keep them; Expr trims them for ergonomics), no whitespace. Abstract syntax means exactly this thinning-out.
