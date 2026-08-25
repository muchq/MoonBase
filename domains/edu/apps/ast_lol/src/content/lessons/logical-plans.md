## From what to how

A `Select` tree says what the user wrote. It does not say what to *do* — in what order, over which intermediate results. That second tree is the **logical plan**: a dataflow of operators from **relational algebra**, each consuming rows and producing rows. It is what `EXPLAIN` shows you in Postgres, what Calcite manipulates, what every query engine optimizes. Plans, not ASTs, because plans compose and rearrange: "run the filter earlier" is a tree rotation on a plan and nearly unsayable on an AST.

AstQL's algebra, one node per idea:

```js
{ type: 'Scan',    table, binding, columns }   // read a table's rows
{ type: 'Join',    left, right, on }           // pair rows, keep matches
{ type: 'Filter',  input, predicate }          // keep rows where true
{ type: 'Project', input, columns: [{expr, name}] }  // compute output columns
{ type: 'Sort',    input, keys }
{ type: 'Limit',   input, count }
```

## The naive plan

Your planner performs a fixed, honest translation — the *naive* plan, one canonical stack:

```text
Limit                  ← if LIMIT
  Project              ← always: the SELECT list
    Sort               ← if ORDER BY
      Filter           ← if WHERE
        Join            ⟍ one per JOIN, left-deep,
          Join           ⟋ in source order
            Scan(from)
            Scan(join 1)
          Scan(join 2)
```

Reading bottom-up, it is SQL's logical evaluation order: gather rows, join, filter, sort, project, truncate. Three decisions deserve their reasons:

- **Sort sits *below* Project.** `SELECT name FROM products ORDER BY price` sorts by a column the output drops. Sorting full-width rows first, projecting after, makes that legal without special cases. (Real engines also resolve the tension this way — then optimize the width back down, as you will in Tier 5.)
- **Joins are left-deep, in source order.** `A JOIN B JOIN C` is `Join(Join(A,B),C)`. No cleverness about which order would be *cheaper* — that is optimizer business, and keeping the planner dumb is what gives the optimizer a stable, predictable input.
- **Project always exists,** naming its outputs: the alias if given, a column's own name, else `col1`, `col2`… positionally. Every plan thus ends by declaring its output schema.

Inside the plan, rows carry **binding-qualified keys** — `u.id`, `o.total` — matching the resolved `Column` nodes; the final Project renames to user-facing output names. One more elaboration: each Scan records its binding and column list, so executing a plan needs no catalog at its elbow.

## Plans are inspectable values

The plan is plain data, like every tree in this course — walk it, print it, diff it, count its nodes. Tier 5's grader leans on this: it *executes* your rewritten plans against reference results and *measures* them with an instrumented executor. Before that, though, plans have to run at all. Next lesson: making rows actually flow.
