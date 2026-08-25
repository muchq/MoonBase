## Transforms that keep their word

A pretty-printer and a constant folder look like unrelated utilities. They are the same thing: functions from trees to *equivalent* artifacts. The printer must emit text that **parses back to the identical tree**; the folder must emit a tree that **evaluates to the same number in every environment**. That obligation — meaning is preserved — is what distinguishes a *rewrite* from a bug, and every serious AST tool lives under it: formatters, minifiers, optimizing compilers, query planners.

## Printing is precedence in reverse

The parser used precedence to decide where invisible parentheses go; the printer must decide where *visible* ones are required. The rule is mechanical once stated:

> Parenthesize a child when its precedence is **lower** than its parent's — or **equal**, on the side the parent does not associate toward.

So `Add(1, Mul(2,3))` prints `1 + 2 * 3` (child tighter: bare), but `Mul(Add(1,2), 3)` prints `(1 + 2) * 3` (child looser: wrapped). The equal-precedence half is where the interesting cases live: `Sub(1, Sub(2,3))` must print `1 - (2 - 3)`, because bare `1 - 2 - 3` reparses left-leaning. For right-associative `^`, the same rule mirrors: parens on the *left* at equal precedence.

The grader for the printer challenge exploits the contract directly: when your string is wrong, it reparses your output and shows you **which tree your text actually encodes**. That is the round-trip property used as a debugging tool — worth internalizing, because "print, reparse, compare" is also how you test real printers. And when a paren case surprises you, add it as a custom test before you fix it: the failing input *is* the specification of the bug, and keeping it in the bank is what makes the fix permanent.

## Folding, bottom-up

Constant folding replaces computation-on-constants with its result: `x + 2 * 3` becomes `x + 6`. The shape is the rebuild traversal in **exit order**: fold the children first, then ask whether this node's freshly-folded children are both literals.

```text
fold(Binary(+, Num 1, Binary(*, Num 2, Num 3)))
  -> children first: Num 1, Num 6
  -> both literal   : Num 7
```

Doing it top-down misses everything (the parent checks its children before they have folded); recursing but forgetting to *use* the folded children (`ast.left` instead of `left`) folds one level only. Both mistakes are in the test bank.

## The unsafe rewrite

Here is the trap this lesson exists for. `1 / 0` in Expr evaluates to `Infinity`. May the folder replace `Div(1, 0)` with `Num(Infinity)`?

Equivalence says yes — same value. But `Num(Infinity)` cannot be *printed*: no Expr literal spells Infinity, so the folded tree breaks the printer you wrote an hour ago. The rewrite is semantics-preserving and still wrong, because it leaves the space of trees the rest of the toolchain can handle. Expr's rule: **fold only when the result is a finite number**.

This is a miniature of a rule that gets real in Tier 5: a rewrite must preserve meaning *under the semantics that will actually run*, not the semantics you have in your head. `0 * x → 0` looks obviously safe — until `x` can be `NaN` (JavaScript) or `NULL` (SQL), where it changes the answer. The optimizer tier is largely the discipline of proving such things before applying them; there, an instrumented executor will check you.
