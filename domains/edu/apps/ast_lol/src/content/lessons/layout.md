## The other capstone

This track builds the tool named first in this course's first sentence: a **code formatter**. It is the mirror image of the optimizer. Both consume a tree and produce something equivalent under a hard obligation — the optimizer must not change *the answer*; the formatter must not change *the program*. And both are judged against a resource: the optimizer against a cost meter, the formatter against a **line width**.

You need only Tier 3 to be here. A formatter works on the parsed tree and never asks what anything *means* — no catalog, no plans, no execution. That independence is the point of making it a separate track: it exercises printing and layout, a different muscle than semantics.

## Canonical style is a spec, not a taste

Prettier's central insight (and gofmt's before it): a formatter should not preserve the author's layout and tidy it — it should **discard the original layout entirely and reprint the tree in one canonical style**. Two queries that parse the same must format the same, byte for byte. That is only possible if the style is pinned down to the last decision, which is why this track's statements read like law: `AS` for every alias, `ASC` omitted because it is the default, trailing commas on broken lists, `AND` leading its continuation line.

The style itself is argued about forever (leading vs. trailing commas is a genuine SQL holy war); *having exactly one* is the engineering content. Black's style document is a masterclass in what pinning a style down actually takes.

## The obligation: reparse identity

A formatter that changes meaning is a bug factory of the worst kind — silent, and in *source control*. The contract is mechanical: **parse(format(tree)) must equal tree**. You built the machinery for this in Tier 2's pretty-printer, and it comes back at SQL scale: minimal parentheses under a precedence system that now includes `NOT`, `IS NULL`, and one delightful trap — `-(-a)` must keep its parentheses because bare `--` lexes as a comment. The grader leans on the contract the same way: when your output is wrong, it reparses your text and tells you *which tree you actually printed*.

## Width-aware layout

The algorithmic heart of any formatter is the fit-or-break decision. This course uses the greedy form of it, which is also what most production SQL formatters do:

1. Render the whole query **flat**. If it fits the width — done. Most queries end here.
2. Otherwise, each clause starts its own line, and each clause makes the *same decision again* at its own scale: a `SELECT` list inlines or breaks one-item-per-line; a `WHERE` predicate inlines or splits its top-level `AND`/`OR` chain, operator leading each continuation.
3. Below that, stop: expressions render flat, and a single expression wider than the limit is **unavoidable overflow**, accepted rather than mangled.

The recursion-with-a-budget shape — try wide, fall back to tall — is exactly Wadler's *prettier printer* algebra with the groups chosen for you. The paper (linked below) generalizes it: every "group" independently chooses flat or broken against the remaining width. Prettier, dartfmt, and most modern formatters are elaborations of that one idea; Bob Nystrom's essay on dartfmt is the honest account of how deep the elaborations go when a language has real statements, comments, and chained calls.

What makes the capstone satisfying is watching one query pass through widths: at 300 it is a single line; at 60 the clauses separate and the `WHERE` chain unstacks; at 28 the lists shatter too — and at every width, reparse identity holds.
