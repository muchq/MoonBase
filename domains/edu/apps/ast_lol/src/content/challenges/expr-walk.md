Write `trace(ast)` to record a traversal as an array of strings. This is a small visitor with observable enter and exit hooks.

### Predict before coding

For `x + -2`, predict which event immediately precedes `exit:Binary`. It is `exit:Unary`: the complete right subtree finishes before the parent.

### Contract

Emit `enter:` followed by the node's `type` before visiting its children. Visit `Binary.left` before `Binary.right`; visit `Unary.operand`; `Num` and `Var` are leaves. Emit `exit:` followed by the type after all children. Do not mutate the tree.

For `-x`, return:

```js
['enter:Unary', 'enter:Var', 'exit:Var', 'exit:Unary']
```

### Debugging notes

If a parent exits before a child, move the exit event below recursion. If the right half is missing, check both Binary fields. Keep one collector for the entire walk.

### Transfer

Which events would you use to compute nesting depth? Which would you use to fold a constant parent whose children have just folded? Enter supports the first; exit supports the second. The next challenges reuse this distinction.
