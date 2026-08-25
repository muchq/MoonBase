# ast.lol

A tiered, auto-graded course on AST parsing and transformation for experienced
programmers new to the area, building to SQL query parsing and rule-based plan
optimization. React + TypeScript SPA built with Vite and deployed to
**Cloudflare Workers** at [ast.lol](https://ast.lol). Fully client-side: the
grader runs the user's JavaScript in a browser Web Worker; nothing is uploaded.

## Shape of the thing

- **Curriculum** (`src/curriculum/`) — 6 tiers, 13 lessons, 16 challenges.
  Tiers 1–2 build a toy expression language (tokenize → parse → evaluate →
  traverse → print → fold); tiers 3–5 build AstQL, the course's SQL subset
  (tokenize → Pratt-parse → resolve → plan → execute → optimize), ending in a
  capstone optimizer graded on result equivalence *and* a cost budget.
  Each challenge carries a test bank with per-test debugging hints, a starter,
  and a reference solution that doubles as the grading oracle.
- **Reference implementations** (`src/lang/`) — the TypeScript
  implementations of both languages. Challenge test banks build their inputs
  and pinned expectations from these, so the curriculum cannot drift from the
  library that grades it. `sql/execute.ts` carries the instrumented cost model
  (cells entering each operator) the optimizer tier is budgeted against.
- **Grader** (`src/grader/`) — a pure synchronous harness (`harness.ts`) that
  runs both under vitest and inside a module Web Worker (`worker.ts`), with a
  main-thread client (`client.ts`) enforcing time budgets by worker
  termination. Failure output is the product: first-difference paths into
  structures, per-test console capture, error lines mapped back to user code,
  and oracle-graded **user-authored custom tests** (input only — the reference
  solution supplies the expected answer).
- **Content** (`src/content/`) — lessons and challenge statements as
  markdown, rendered to HTML at build time by `vite-plugin-content.ts` so no
  markdown parser ships in the bundle.

Submissions are JavaScript; the grader takes a `language` field and compiles
per-language, so TypeScript (a transpile step) or a functional language can be
added without reshaping anything.

## Develop locally

```bash
npm install
npm run dev        # Vite dev server with Cloudflare Workers runtime
```

## Test & typecheck

```bash
npm test           # Vitest (259 tests)
npm run typecheck  # tsc --noEmit
```

CI-relevant invariants the suite pins: every reference solution passes its own
test bank, every starter fails it, every custom-test placeholder builds an
input the reference solution accepts, every lesson/challenge id has a rendered
document (and no orphans), lesson reading links are https, the capstone's
hardcoded cost budgets match the reference pipeline (and sit strictly below
the naive cost, so a no-op optimizer cannot pass a reduce query), the bench
database covers its full value domains, and the SQL parser replays a frozen
corpus (`src/__tests__/corpus/`; regenerate deliberately with
`UPDATE_SQL_CORPUS=1 npm test` and review the diff as a spec change).

## Build

```bash
npm run build
# Outputs:
#   dist/client/        — SPA static assets (served by Workers Assets)
#   dist/ast_lol/       — Worker bundle + generated wrangler.json
```

## Deploy (Cloudflare Workers)

```bash
npm run build
npx wrangler deploy --config dist/ast_lol/wrangler.json
```

For Cloudflare CI in the Workers dashboard:

- **Build command:** `npm ci && npm run build`
- **Deploy command:** `npx wrangler deploy --config dist/ast_lol/wrangler.json`
- **Root directory:** `/domains/edu/apps/ast_lol`

To serve on the apex domain, add `ast.lol` to the Cloudflare account, then
uncomment the `routes` line in `wrangler.toml` (or attach the custom domain to
the worker in the dashboard). `html_handling = "none"` is deliberate — see the
comment in `wrangler.toml`.

## Docs

- [`docs/CURRICULUM.md`](docs/CURRICULUM.md) — learning-path design: tier
  ordering rationale, problem selection, grading philosophy, and the cost
  model.
- [`docs/ROADMAP.md`](docs/ROADMAP.md) — deliberately deferred work.
