## Why ASTs

You have used the products of this technique all week: the formatter that reflowed your code, the linter that flagged the unused import, the bundler that tree-shook your dependencies, the database that answered a query faster than it had any right to. Every one of them works the same way — it **parses text into a tree, reasons about the tree, and often rewrites it**.

That tree is the *abstract syntax tree*: the structure of a program with the incidental details of its spelling — whitespace, comments, parenthesization — boiled away. Working with ASTs is a distinct skill from ordinary application programming, and it is a learnable one. This course teaches it from scratch, with an auto-grader, to programmers who are already fluent in a general-purpose language.

## The route

The course builds two languages:

1. **Expr** (Tiers 1–2) — arithmetic expressions with variables: `-(x + 1) * y ^ 2`. Small enough to hold in your head, rich enough to force the real issues: precedence, associativity, error reporting, traversal, meaning-preserving rewrites.
2. **AstQL** (Tiers 3–5) — a SQL subset: `SELECT`, `JOIN … ON`, `WHERE`, `ORDER BY`, `LIMIT`. Real enough to be worth optimizing. You will tokenize it, Pratt-parse it, resolve names against a catalog, lower it to relational-algebra plans, execute those plans with proper `NULL` semantics — and then make them measurably cheaper.

The finale is a working query optimizer: predicate simplification, filter pushdown, and column pruning, graded on two things a real optimizer is graded on — **it must not change the answer**, and **it must beat a cost budget** measured by an instrumented executor.

Nothing here requires prior compiler or database experience. Each tier builds exactly the skills the next tier spends.

## How grading works

Every challenge gives you a function contract and a bank of named tests. Your code runs sandboxed in a Web Worker in your own browser — nothing is uploaded anywhere. The grader is built to help you debug, not just to judge:

- failures point at the **first structural difference** between expected and actual (`result[3].kind: expected "op", got "ident"`), not just "wrong";
- `console.log` output is captured **per test**;
- runtime errors are mapped back to **line numbers in your code**;
- tests carry **hints** written against the classic mistakes;
- you can **add your own test cases**: type an input, and the reference solution computes the expected answer for it.

Solutions are plain JavaScript, checked as you submit. A "reveal solution" button exists on every challenge — the reference implementations are meant to be read, ideally after your own attempt goes green.

If you get stuck: read the failing test's name and hint first, then `console.log` the structure you are building. Tree bugs are almost always visible in the first place the shapes diverge.
