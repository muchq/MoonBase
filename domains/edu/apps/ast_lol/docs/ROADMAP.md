# Roadmap

Next depth extensions, after the learning-path and practice improvements.

- **Source spans and diagnostics.** Preserve source locations on AST nodes;
  add a diagnostic repair exercise and link tree selection back to source.
- **Formatter algebra and comments.** Introduce text/line/indent/group documents,
  then preserve comments. The existing formatter lab exposes the width cascade;
  a richer document algebra remains a separate extension.
- **Optimizer extensions.** Add an empty-relation node and contradiction
  propagation. Before join reordering, define bag equality versus ordered output;
  the current exact-array contract intentionally preserves execution order.
- **Language depth.** Add typed catalogs, then aggregation or nested scopes as
  separate advanced projects with their own semantics and regression banks.
- **Observe real learning.** Watch learners attempt the new milestones and
  tune the approximate effort ranges, question wording, and hint timing.

- **TypeScript submissions.** The grader's `Submission.language` field and
  per-language `compile` switch exist for this; the work is a transpile step
  (likely `sucrase` or `esbuild-wasm`, lazy-loaded in the worker) plus an
  editor language toggle.
- **A functional language.** Same seam. A small ML-flavored language would
  need its own compile step targeting JS; the harness, test banks, and oracle
  flow are language-agnostic already.
- **Code splitting.** The pipeline lab loads on demand; CodeMirror and the
  curriculum could load per-route (`import()` in the challenge view) if
  first-paint on slow connections starts to matter.
- **Further capstone candidates.** Join reordering against the cost model
  (the natural next optimizer rule); `GROUP BY` + aggregate pushdown; an
  index-aware Scan with a chooser; comment-preserving formatting (the classic
  hard part the Tier 6 formatter deliberately omits — AstQL discards comments
  at the tokenizer). These require explicit extensions to the algebra and its equivalence contract.
- **Shareable progress.** Progress is localStorage-only by design (no
  backend). An export/import blob would let people move browsers without
  adding infrastructure.
- **Corpus growth.** `sql-corpus.json` is seeded with 27 queries; grow it
  whenever a parsing bug is found in the wild — the fix's regression test is
  a corpus line.
