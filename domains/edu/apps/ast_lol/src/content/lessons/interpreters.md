## The smallest interpreter

Once syntax is a tree, *meaning* is a recursion over it. An evaluator for Expr is a single function — the base cases return values, the recursive cases combine their children's values:

```js
function evaluate(ast, env) {
  switch (ast.type) {
    case 'Num':    return ast.value;
    case 'Var':    return /* look up ast.name in env */;
    case 'Unary':  return -evaluate(ast.operand, env);
    case 'Binary': /* evaluate both sides, apply ast.op */;
  }
}
```

This is a **tree-walking interpreter** — the design at the heart of the original Ruby and PHP implementations, of most template engines and query engines, and of every DSL you have embedded in a config file. It is rarely the *fastest* design (that is what bytecode VMs and JITs are for), but it is the clearest, and for tree-shaped domain languages it is usually the right call. Tier 4's query executor is this same function, scaled up to rows and plans.

Notice what the evaluator does **not** do: it never thinks about precedence. `1 + 2 * 3` arrives as `Add(1, Mul(2, 3))` — the parser already made every ordering decision. This separation is the payoff of the pipeline: each stage handles one concern and hands a cleaner structure to the next.

## Environments

Variables get their values from an **environment** — here, a plain object mapping names to numbers. Two sharp edges are worth respecting even at this size:

- **Absence is not falsiness.** `{ x: 0 }` binds `x` to a perfectly good value; `env[name] || throw` would reject it. Check presence, then read.
- **Inherited properties are not bindings.** `'constructor' in env` is `true` for every plain object in JavaScript. Use an own-property check (`Object.prototype.hasOwnProperty.call(env, name)`), or a prototype-less container. Real interpreters use dedicated scope structures for exactly this class of reason — and in Tier 4, "the environment" becomes a database catalog with its own resolution rules.

Unknown variables throw, with the name in the message: `Unknown variable 'b'`. Like the tokenizer's errors, this is pinned by tests — precise failure is a feature.

## Semantics are decisions

`1 / 0` — what happens? Expr's answer: whatever JavaScript says (`Infinity`). That is a *decision*, not an accident, and this course makes it twice, differently: AstQL in Tier 4 defines division by zero as `NULL`, following SQL. Neither answer is "correct"; a language's semantics are the sum of such choices, and an evaluator is where they become executable. When you write the SQL executor later, watch how many of these small decisions (`NULL` comparisons, sort order of missing values) turn out to dominate the work.
