Write `execute(plan, db)` — run a logical plan over an in-memory database and return the rows.

`db` maps table names to arrays of row objects with plain column keys. Inside the plan, rows use **binding-qualified** keys (`'u.id'`); the final Project renames to output names.

### Expression evaluation (three-valued logic)

| form | result |
|---|---|
| `Column` | `row[table + '.' + name]`; `undefined` reads as `null` |
| `+ - *` | `null` unless both sides are numbers |
| `/` | as above, and division by zero → `null` (not `Infinity` — this is AstQL) |
| `= <> < <= > >=` | `null` if either side is `null` **or** types differ; else JS comparison |
| `AND` | `false` if either side is `false`; else `null` if either is `null`; else `true` |
| `OR` | `true` if either side is `true`; else `null` if either is `null`; else `false` |
| `NOT` | `null` stays `null`; otherwise `v !== true` |
| `-x` | `null` unless `x` is a number |
| `IS [NOT] NULL` | always `true` or `false` |

### Operators

- **Scan** — one output row per table row, keys `binding + '.' + column` over the Scan's column list.
- **Join** — nested loop: for each left row (in order), for each right row (in order), merge `{ ...l, ...r }` and keep the pair only when `ON` is exactly `true`.
- **Filter** — keep rows where the predicate is exactly `true` (`null` drops).
- **Sort** — stable, multi-key; compare non-null keys normally, flip for `DESC`, and **nulls order last regardless of direction**.
- **Limit** — first `count` rows. **Project** — evaluate each column per row, output under its `name`.

### Debugging notes

- `null AND false` must be `false` — evaluate both sides, don't short-circuit on null. The Kleene test is named after this.
- The nulls-last DESC test: handle nulls **before** applying the direction flip.
- Row order is part of the contract everywhere (join order, stable sort). If rows match but order doesn't, the first-difference path will point at the first misplaced row index.
- `console.log` a row when a predicate misbehaves — seeing `{ 'u.city': null }` next to your comparison usually ends the mystery.
