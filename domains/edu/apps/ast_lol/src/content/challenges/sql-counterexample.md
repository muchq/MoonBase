Write `counterexample(before, after)` to search for a row where two expressions differ. This extension asks you to challenge a rewrite rather than copy an optimizer pass.

### Contract

The expressions use at most two distinct **qualified** column names. Search assignments from this finite domain:

```js
[null, false, true, 0, 1, 'x']
```

Use the union of names from both expressions. Return any row containing exactly those keys and domain values for which the expression results differ according to `Object.is`. Return null if every assignment agrees. With no columns, evaluate once against `{}`. Leave input trees unchanged.

`evalExpr(expr, row)` and `columnNames(expr)` are provided. One expression may refer to a column the other does not. The grader accepts any valid witness; it does not require the reference search order.

### Work one example

For `TRUE AND u.x` versus `u.x`, `{ 'u.x': 0 }` is a witness: false and zero differ. For `u.x = v.y` versus `v.y = u.x`, every assignment in this domain agrees.

### Debugging notes

Search the Cartesian product, not just rows where both columns have the same value. Copy a row before changing it for the next branch, or carefully restore state. Compare with `Object.is` so NaN is treated consistently.

### What passing means

A witness disproves universal equivalence. No witness proves agreement only over this finite domain; it is not proof over every number, string, or database. After finding a witness, explain the missing precondition and add it as a custom regression test to the affected pass.
