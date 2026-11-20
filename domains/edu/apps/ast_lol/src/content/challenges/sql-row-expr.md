Write `evaluateRow(expr, row)` for a single AstQL expression. This milestone isolates expression semantics from plan execution.

### Contract

Use the expression rules in the execution lesson. `Lit` returns its value. Qualified columns read `row[table + '.' + name]`; missing values become null. Arithmetic requires numbers; division by zero yields null. Comparisons yield null for null operands or differing types. Unary minus requires a number. `IS [NOT] NULL` always returns a boolean.

For logical operators, coerce each operand: true stays true, null stays null, everything else becomes false. AND: false wins, then null, otherwise true. OR: true wins, then null, otherwise false. NOT preserves null and flips the coerced boolean. Leave both inputs unchanged.

### Work one example

With `{ 'u.city': null }`, `NOT (u.city = 'x' AND FALSE)` becomes `NOT (null AND false)`, then `NOT false`, then true. Returning null as soon as you see a null operand is incorrect.

### Debugging notes

Get the 18 AND/OR truth-table tests green first. Then add scalar operators and column lookup. A truthy JavaScript value is not an AstQL true value: `TRUE AND 5` is false.

### Transfer

Explain why replacing `TRUE AND u.id` with `u.id` could change a later comparison. Your optimizer will need a type precondition.
