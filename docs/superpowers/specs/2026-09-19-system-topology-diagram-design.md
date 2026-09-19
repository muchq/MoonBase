# System and Dataflow Diagram

**Status:** Design
**Date:** 2026-09-19
**Scope:** One hand-maintained mermaid diagram of the consolidated deployment in this repo, and an interactive page in the muchq.com UI that renders the same source.

The diagram itself is [`docs/DEPLOYMENT.md`](../../DEPLOYMENT.md). This records
the decisions behind it and the alternatives that were rejected.

## Background

`compose.yaml` says which containers exist and `Caddyfile` says which public
name reaches which backend, but neither expresses the edges that matter most:
deja's predictions feeding the lobby, Caddy's access log feeding deja and
stats, `/tracy` reaching portrait only through a browser.

## Non-goals

- **Generating it from `compose.yaml` / `Caddyfile`.** Those files cannot
  express the semantic edges, and a generator guessing them from env-var names
  would be wrong in ways nobody could see.
- **A CI gate on drift.** Declined deliberately. If staleness bites, a Go test
  beside `deploy_config_test.go` pinning the node set against `compose.yaml` is
  the cheapest fix and needs no format change.
- **Traffic volume, latency, per-edge throughput.** `/metrics` owns that.

## Decisions

**Node ids are compose service names** wherever a container exists. That makes
each node a claim about a real container and gives the UI's status overlay its
join key. Nodes with no container — muchq.com routes, public names, S3 — are
the exception, and the overlay skips them rather than reporting them unknown.

**Every edge carries its kind as the label**, one of `http`, `sql`, `ssh`,
`events`, `logs`, `metrics`, `ui`. It makes the UI's layer filter a one-line
text match and keeps the GitHub render readable without a legend. Route detail
lives in tables, not on arrows.

**Direction follows the label**, not the call: `http`/`sql`/`ssh` point the way
the call goes, `logs`/`metrics`/`events` the way the data moves. Stating this
matters — a single rule of "arrows follow the data" is false of two thirds of
the edges.

**One-shot init and migrate jobs are omitted** from the graph and tabled
instead. They sequence a deploy rather than carry data.

## The UI page

New `src/apps/topology/` in the muchq.com repo, route `/topology`. Not
`/system`: `SystemsPage` already exists and is the resilience game.

```
src/apps/topology/
  topology.mmd              copy of the mermaid block
  pages/TopologyPage.tsx
  components/TopologyGraph.tsx
  components/LayerToggles.tsx
  topology.ts               parse, filter, container-name join (pure)
  api.ts                    status fetch, reusing metrics-systems helpers
```

**Rendering.** `await import('mermaid')` inside the route component, so mermaid
stays out of every other route's bundle.

**Click-through.** `click` directives in the mermaid source itself. GitHub
ignores them; the UI gets navigation from the same file that draws the picture.

**Hover.** Hovering a node dims every node and edge not incident to it, by
walking the parsed edge list and toggling a class on mermaid's rendered SVG.

**Layer toggles.** A checkbox per edge kind, filtering `.mmd` lines by label
and re-rendering. Sixty-odd edges at once is a hairball.

**Status overlay.** Node colour by container state, reusing
`metrics-systems/api.ts` (`fetchJson`, `containerState`, `containerDisplayName`).
When the metrics API is unreachable every node goes to `unknown` and the page
still draws.

### The node-to-container join

The one real integration risk. Compose service names and container names do not
reliably match — `microgpt-serve` is hyphenated where its siblings are not, and
mcpserver's own env refers to `one-d4` where the service is `one_d4`. The
mapping is an explicit table in `topology.ts` with a unit test, not string
normalization that silently mismatches and reports a live service as unknown.
Non-container nodes are absent from the table by design, and the join treats
absence as "not a container" rather than "unknown".

## Syncing the copy

`scripts/sync-topology.sh` in the UI repo curls the mermaid block out of
`docs/DEPLOYMENT.md` and writes `topology.mmd`. Nothing runs it automatically,
consistent with the no-gate decision; the UI can lag the doc.

## Testing

Pure functions in `topology.ts` — parsing edges and kinds, filtering by enabled
kinds, joining node ids to container names — get vitest unit tests.
`TopologyPage` gets RTL tests for loading, metrics-unreachable, and a layer
toggle changing the rendered edge set. Mermaid is mocked.

No test lands in MoonBase; the decision above was explicitly not to gate it.

## Work split

Two PRs, MoonBase first so the UI has something to copy.

1. **MoonBase:** `docs/DEPLOYMENT.md`, linked from `CLAUDE.md`.
2. **muchq.com:** `src/apps/topology/`, the `/topology` route, the sync script,
   and the tests.
