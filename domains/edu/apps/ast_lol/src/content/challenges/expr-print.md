Write `print(ast)` — turn a tree back into source text with **minimal parentheses**.

### Formatting

- Binary operators get single spaces: `a + b`.
- Unary minus is tight: `-a`, `--a`.
- No spaces inside parens: `(a + b) * c`.
- `Num` prints via `String(value)`; inputs are trees the parser can produce.

### The parenthesization rule

Wrap a child in parens exactly when reparsing the bare form would change the tree:

- the child's precedence is **lower** than the position requires, where levels are `+ -` (1), `* /` (2), unary `-` (3), `^` (4), atoms (5);
- for **left-associative** ops, the **right** child also wraps at *equal* precedence (`1 - (2 - 3)`);
- for `^` (right-associative), the **left** child wraps at equal precedence (`(2 ^ 3) ^ 2`), and the right side accepts anything at unary strength or tighter bare — `2 ^ -3`, `2 ^ 3 ^ 2`.
- unary minus wraps its operand below level 3: `-(a + b)`, `-(a * b)` — but `-a ^ b` stays bare.

### Grading

Exact string match — but when your string is wrong, the grader **reparses your output** and tells you which tree it actually encodes, with the first structural difference. `parses back to a different tree` means missing parens; `right tree, formatting off` means spacing or extra parens.

### Debugging notes

- Write a `prec(node)` helper and a `wrap(child, minPrec)` helper; the whole printer is those two plus a switch.
- Missing-paren bugs are invisible to the eye — trust the reparse diff, not squinting.
