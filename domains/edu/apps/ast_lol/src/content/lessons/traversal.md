## Trees are for walking

Almost everything you do with an AST after parsing is a traversal: find all the identifiers, measure nesting depth, check a style rule, extract dependencies. The evaluator you just wrote was a traversal that computed a number; the transforms ahead are traversals that build trees. Getting fluent here pays for the whole course.

The plain form is a recursive function with one case per node type:

```js
function visit(e) {
  switch (e.type) {
    case 'Num':    return;
    case 'Var':    /* do something with e.name */ return;
    case 'Unary':  visit(e.operand); return;
    case 'Binary': visit(e.left); visit(e.right); return;
  }
}
```

For a four-node language, write exactly this — inline, no machinery. The discipline that matters is **exhaustiveness**: every node type handled, every child field recursed. The classic traversal bug is a missing `visit(e.right)` that silently ignores half of every subtraction; nothing crashes, results are just quietly incomplete.

## Visitors, when the machinery earns it

Industrial ASTs have dozens of node types, and most passes care about three of them. Hence the **visitor pattern**: a generic walker owns the recursion, and you supply callbacks keyed by node type. Babel plugins are literally objects like `{ BinaryExpression(path) { … } }`; ESLint rules are the same idea over ESTree; tree-sitter exposes cursors over the same shape. The pattern's value is real but it is *packaging* — under every visitor API is the switch statement above. This course keeps you at the switch level so the packaging never becomes a mystery.

Two conventions worth stealing from those ecosystems even in small code:

- **Enter/exit ordering.** Doing work before recursing ("enter") sees parents first; after recursing ("exit") sees children first. Transforms almost always want exit order — you will use it in the constant folder.
- **Accumulate outside the recursion.** Pass a collector (a `Set`, an array) through, or close over it; do not try to merge return values at every level unless the merge *is* the computation.

## Rebuild, don't mutate

The traversals that *change* trees come next, so adopt the rule now: **treat AST nodes as immutable — return new nodes where something changed**.

```js
// rebuild form: children first, then a new node around them
case 'Binary': {
  const left = walk(e.left);
  const right = walk(e.right);
  return { type: 'Binary', op: e.op, left, right };
}
```

Mutation seems harmless until two facts collide: multiple references to a shared subtree, and a pass that changes one of them. Every AST toolchain has a war story here. Rebuilding costs allocations and buys you the ability to reason — and to keep the original tree around, which the graders in this course (and every "before/after" diff view you have ever used) depend on.

The warm-up challenge collects variable names; the two after it are rebuild-form traversals with real specs. All three are the same skeleton with different verbs.
