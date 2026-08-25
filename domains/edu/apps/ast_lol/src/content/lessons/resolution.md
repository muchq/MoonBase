## Syntax is not meaning

`SELECT nam FROM users` parses perfectly. It is still wrong — there is no column `nam`. Catching that is **semantic analysis**: checking the tree against knowledge the grammar cannot carry. For SQL that knowledge is the **catalog** — the schema: which tables exist, with which columns. (PostgreSQL calls this stage the *analyzer*; compilers call the analogous pass name resolution or type checking. Same layer, same position in the pipeline.)

AstQL's catalog is minimal, and this course's sample one is a small shop:

```js
{ users:    ['id', 'name', 'city', 'signup_year'],
  orders:   ['id', 'user_id', 'product_id', 'quantity', 'total', 'year'],
  products: ['id', 'name', 'category', 'price'] }
```

## Bindings, then columns

Resolution runs in two phases, because the second is meaningless if the first fails.

**Phase 1 — establish bindings.** Each `FROM`/`JOIN` entry introduces a *binding*: its alias if it has one, else the table name. `users u` binds `u`; plain `users` binds `users`. Two checks: every table must exist in the catalog, and binding names must be unique (`JOIN orders u` when `u` is taken is an error — which name would `u.id` mean?). Aliasing *replaces* the name: after `users u`, the binding `users` does not exist. Any phase-1 error stops resolution.

**Phase 2 — resolve every column, in clause order.** SELECT list, then each `JOIN`'s `ON`, then `WHERE`, then `ORDER BY`:

- `u.city` — qualified: the binding must exist and own the column.
- `city` — bare: exactly **one** binding must own it. Zero owners: unknown column. Two or more: **ambiguous** — and this error is the reason production SQL style guides say "qualify your columns in joins": add one `JOIN` to a working query and its bare columns can start failing.

## Errors are data here

The parser threw on the first bad token; the resolver **collects**:

```js
{ select: null, errors: [ { kind: 'unknown-column', name: 'u.nam' },
                          { kind: 'ambiguous-column', name: 'id' } ] }
```

The difference is audience. A syntax error means the text is not a program, and continuing is guesswork. A resolution error lives in a valid tree, so the checker can keep walking and report *everything* wrong at once — the difference between fixing five typos in five runs and one. This errors-as-data discipline (with a deterministic order, so tools and tests can rely on it) is how type checkers, linters, and IDE diagnostics all work.

## The annotated tree

On success, resolution returns a *rewritten* tree — the rebuild-form traversal from Tier 2, now doing semantic work:

- every `Column.table` is filled in with its binding (`city` becomes `{ table: 'users', name: 'city' }`),
- `*` expands into the concrete column list, binding by binding, in catalog order.

After this pass, no later stage ever asks "which table does this column belong to?" — the tree simply says. Compilers call this pattern *elaboration*: each stage's output leaves fewer questions open than its input. The planner you write next depends on it completely.
