## Why ASTs

The everyday tools of programming — the formatter that reflows your code, the linter that flags an unused import, the bundler that tree-shakes your dependencies, the database that answers a query faster than it has any right to — all work the same way: each one **parses text into a tree, reasons about the tree, and often rewrites it**.

That tree is the *abstract syntax tree*: the structure of a program with the incidental details of its spelling — whitespace, comments, parenthesization — boiled away. Working with ASTs is a distinct skill from ordinary application programming, and it is a learnable one. This course teaches it from scratch, with an auto-grader, to programmers who are already fluent in a general-purpose language.

## The route

The course builds two languages:

1. **Expr** (Tiers 1–2) — arithmetic expressions with variables: `-(x + 1) * y ^ 2`. Small enough to hold in your head, rich enough to force the real issues: precedence, associativity, error reporting, traversal, meaning-preserving rewrites.
2. **AstQL** (Tiers 3–6) — a SQL subset: `SELECT`, `JOIN … ON`, `WHERE`, `ORDER BY`, `LIMIT`. Real enough to be worth optimizing. You will tokenize it, Pratt-parse it, resolve names against a catalog, lower it to relational-algebra plans, execute those plans with proper `NULL` semantics — and then make them measurably cheaper.

The course ends in **two capstone tracks** — one for each bookend of that opening list of tools:

- **The optimizer** (Tiers 4–5) — resolve names, build logical plans, execute them with honest `NULL` semantics, then make them cheaper: predicate simplification, filter pushdown, and column pruning, graded on the two things a real optimizer is graded on — **it must not change the answer**, and **it must beat a cost budget** measured by an instrumented executor.
- **The formatter** (Tier 6) — the other classic. A width-aware SQL formatter in the Prettier tradition: flat when the query fits, canonical multi-line layout when it doesn't, graded on exact output — with a grader that **reparses your text** to show which tree it actually encodes when you're off.

The formatter track needs only Tier 3: take it before, after, or instead of the optimizer tiers. Doing both is the full course.

Nothing here requires prior compiler or database experience. Each tier builds exactly the skills its later challenges spend.

## How grading works

Every challenge gives you a function contract and a bank of named tests. Your code runs sandboxed in a Web Worker in your own browser — nothing is uploaded anywhere. The grader is built to help you debug, not just to judge:

- failures point at the **first structural difference** between expected and actual (`result[3].kind: expected "op", got "ident"`), not just "wrong";
- `console.log` output is captured **per test**;
- runtime errors are mapped back to **line numbers in your code**;
- tests carry **hints** written against the classic mistakes;
- you can **add your own test cases**: type an input, and the reference solution computes the expected answer for it.

Solutions are plain JavaScript, checked as you submit. A "reveal solution" button exists on every challenge — the reference implementations are meant to be read, ideally after your own attempt goes green.

If you get stuck: read the failing test's name and hint first, then `console.log` the structure you are building. Tree bugs are almost always visible in the first place the shapes diverge.
