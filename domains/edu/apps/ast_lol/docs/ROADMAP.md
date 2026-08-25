# Roadmap

Deliberately deferred from v1, roughly in value order.

- **TypeScript submissions.** The grader's `Submission.language` field and
  per-language `compile` switch exist for this; the work is a transpile step
  (likely `sucrase` or `esbuild-wasm`, lazy-loaded in the worker) plus an
  editor language toggle.
- **A functional language.** Same seam. A small ML-flavored language would
  need its own compile step targeting JS; the harness, test banks, and oracle
  flow are language-agnostic already.
- **Code splitting.** The main bundle is ~272 kB gzipped; CodeMirror and the
  curriculum could load per-route (`import()` in the challenge view) if
  first-paint on slow connections starts to matter.
- **Further capstone candidates.** Join reordering against the cost model
  (the natural next optimizer rule); `GROUP BY` + aggregate pushdown; an
  index-aware Scan with a chooser; comment-preserving formatting (the classic
  hard part the Tier 6 formatter deliberately omits — AstQL discards comments
  at the tokenizer). Each fits the existing algebra.
- **Shareable progress.** Progress is localStorage-only by design (no
  backend). An export/import blob would let people move browsers without
  adding infrastructure.
- **Corpus growth.** `sql-corpus.json` is seeded with 27 queries; grow it
  whenever a parsing bug is found in the wild — the fix's regression test is
  a corpus line.
