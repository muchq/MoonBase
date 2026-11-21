Write `isPredicate(expr)` to conservatively prove that an AstQL expression returns only true, false, or null. This is a small static analysis, not an evaluator.

### Contract

Return true for boolean/null literals, comparisons (`= <> < <= > >=`), AND, OR, NOT, and `IS [NOT] NULL`. Return false for number/string literals, bare columns, arithmetic, and unary minus. The catalog contains column names but no types, so a column's name is never enough evidence. Do not mutate the expression.

### Why the proof matters

`TRUE AND u.id` returns false for a numeric id. Replacing that expression with `u.id` changes its value. However, `TRUE AND (u.id > 0)` can safely become `u.id > 0`: a comparison returns boolean or null.

The outer operator decides the result class. `u.id AND 5` is a predicate even though its operands are not predicates, because AND coerces its inputs.

### Debugging notes

Do not recursively require all operands to be predicates. Do not infer a column type from a name such as `is_active`. Recognize all six comparison operators.

### Transfer

Use this proof on the simplified child before eliminating TRUE from AND or FALSE from OR. What catalog information would allow you to prove more expressions safe?
