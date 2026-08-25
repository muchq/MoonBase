Write `evaluate(ast, env)` — a tree-walking evaluator returning a number.

### Semantics

- `Num` → its `value`; `Var` → the value bound in `env` (a plain object like `{ x: 4 }`).
- `Unary` → the negation of its operand's value.
- `Binary` → evaluate both sides, then apply: `+ - * /` as in JavaScript (yes, `1 / 0` is `Infinity` — this is Expr, not SQL), `^` is exponentiation.
- Unknown variable: `throw new Error("Unknown variable '" + name + "'")`.

### The environment, precisely

- **Presence, not truthiness**: `{ x: 0 }` binds `x` to a real value. `env[name] || …` is a bug.
- **Own properties only**: `'constructor' in env` is `true` for every plain object. Use `Object.prototype.hasOwnProperty.call(env, name)` — there is a test named after this.

### Example

```js
evaluate(parse('x ^ 2 + y * 3'), { x: 4, y: 2 })  // 22
```

### Debugging notes

- No precedence logic belongs here — the tree already encodes it. If you're inspecting operators' relative strength in the evaluator, back up.
- The evaluator is 20-ish lines. If yours is growing, you are probably re-solving a parser problem.
