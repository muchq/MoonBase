Write `simplifyPlan(plan)` — simplify every predicate in the plan (Filter predicates and Join `ON`s), bottom-up, under three-valued logic.

### Expression rules

1. **Strict operators** (`+ - * /`, comparisons): both sides `Lit` → fold to a `Lit` using the executor's semantics (`4 / 0` → `Lit null`, mismatched types compare to `null`). *Either* side `Lit null` → `Lit null` — strict operators are null-in, null-out, no data required.
2. **AND**: a `Lit false` side → `Lit false` (Kleene: false absorbs even next to `null`). A `Lit true` side → the other side **only if that expression is proven boolean-or-null**. Both `Lit` → fold.
3. **OR**: mirror — `Lit true` absorbs, `Lit false` drops out only beside a proven boolean-or-null expression.
4. **`NOT` / unary `-` / `IS [NOT] NULL`** over a `Lit` → fold.

Nothing else changes; column-only expressions come back untouched.

### Plan rules

- A Filter whose predicate became `Lit true` **disappears** (return its input).
- A Filter that became `Lit false` or `Lit null` **stays** — it correctly keeps zero rows, and this algebra has no empty-relation node to replace it with.
- Join `ON`s simplify in place; every other node just rebuilds around its simplified input. Never mutate.

### Debugging notes

- Simplify children **first**, then apply this node's rules to what came back — same exit-order shape as the constant folder.
- The Kleene test (`city IS NULL AND FALSE`) fails if your AND rule checks for two Lits before checking for a false side.
- Fold with **Tier 4's semantics**, not JavaScript's: `4 / 0` folds to `null`, and `NULL IS NULL` folds to `true`.
- Grading is structural (deep-equal against the reference transform) — the first-difference path points into the exact predicate subtree that differs.

### Prove the identity rule first

A boolean-or-null expression is a boolean/null literal, a comparison, `AND`, `OR`, `NOT`, or `IS [NOT] NULL`. A bare column has unknown type; arithmetic and unary minus are not predicates. Classify the **simplified** child before dropping an identity literal.

`TRUE AND id` coerces a numeric id to false. Replacing it with `id` changes `(TRUE AND id) = id` from a null comparison into a true one. Keep the original operator unless the precondition holds. When both children are literals, evaluate them with the executor's semantics.
