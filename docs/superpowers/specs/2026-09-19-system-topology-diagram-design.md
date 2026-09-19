# System and Dataflow Diagram

**Status:** Design
**Date:** 2026-09-19
**Scope:** One hand-maintained mermaid diagram of the consolidated deployment in this repo, and an interactive page in the muchq.com UI that renders the same source.

## Background

Nothing in either repo draws the deployed system. `compose.yaml` says which
containers exist and `Caddyfile` says which public route reaches which backend,
but neither expresses the edges that matter most to a reader: deja's
predictions feeding the lobby, Caddy's access log feeding deja and stats, tracy
reaching portrait only through the browser.

## Non-goals

- Generating the diagram from `compose.yaml` / `Caddyfile`. Those files cannot
  express the semantic edges, and a generator that guessed them from env-var
  names would be wrong in ways nobody could see.
- A CI gate on drift. Deliberately declined: this is a hand-maintained document.
  If staleness bites, a Go test beside `deploy_config_test.go` pinning the node
  set against `compose.yaml` is the cheapest fix and needs no format change.
- Rendering runtime traffic volume, latency, or per-edge throughput. The
  `/metrics` dashboard already owns that.

## The source

`docs/ARCHITECTURE.md`, one `flowchart LR` block.

**Node ids are compose service names.** That makes each node a claim about a
real container, and it is the join key the UI's status overlay needs.

**Every edge is labelled with its kind**, one of `http`, `sql`, `events`,
`logs`, `metrics`, `ui`:

```
caddy -->|http| deja
deja ==>|events| games_hub
caddy -.->|logs| deja
games_hub -->|sql| shared_postgres
ui_tracy -.->|ui| portrait
```

The label is the kind and nothing else. It is what makes the UI's layer filter
a one-line text match, and it keeps the GitHub render readable without a
legend. Specific route paths live in a table under the diagram, not on arrows.

### Nodes

Containers from `compose.yaml`: `caddy`, `games_hub`, `portrait`, `prom_proxy`,
`mithril`, `posterize`, `one_d4_v2`, `iili`, `mcpserver`, `one_d4`,
`one_d4_worker`, `microgpt-serve`, `deja`, `stats`, `log_shipper`,
`shared_postgres`, `forgejo`.

From `docker-compose.observability.yml`: `otelcol`, `prometheus`.

Not containers, but required for the edges to make sense: `s3` (the stats
bucket, external), and the muchq.com routes that are the only way a given
service is reached — `ui_tracy`, `ui_lobby`, `ui_posterize`, `ui_wordchains`,
`ui_iili`, `ui_stats`, `ui_deja`.

The `ui_` prefix is load-bearing in two ways. It carries the fact that the
UI's route names and the service names disagree — `/tracy` reaches `portrait`,
`/wordchains` reaches `mithril` — which is precisely the mapping a reader
cannot recover from either repo alone. And it is what the status overlay keys
off: a `ui_` node has no container, so the join skips it rather than reporting
it perpetually unknown.

**Omitted:** the one-shot jobs `games_hub_db_init`, `iili_db_init`,
`one_d4_migrate`, `stats_db_init`. They are deploy-time ordering, not
dataflow; four leaf nodes that explain nothing about how the system runs. They
get a row in the table instead.

**Profile-gated services get a distinct node style.** `stats` and `log_shipper`
sit behind the `stats` compose profile and do not start on a default
`up -d` — a reader who does not know that will look for a container that was
never meant to be running.

### Edges worth stating explicitly

The log path is the one a reader will not guess:

```
caddy writes /var/log/caddy/access.log
  ├── deja tails it read-only (next-request prediction)
  └── log_shipper rolls it to S3, deleting after upload
        └── stats reads S3, aggregates into shared_postgres, serves /stats
```

`one_d4` query events ride the same shipper under their own S3 partition.
`mcpserver` calls `one_d4` and `one_d4_v2` over http. `games_hub` calls `deja`
(`DEJA_URL`). Five services hold `sql` edges to `shared_postgres`:
`games_hub`, `iili`, `one_d4`, `one_d4_worker`, `stats`. Every instrumented
service has a `metrics` edge to `otelcol`; `prom_proxy` reads `prometheus`.
The `ui` edges — `ui_tracy → portrait`, `ui_wordchains → mithril` and the
rest — exist only through the browser; no container talks to another that way.

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

**Click-through.** `click` directives in the mermaid source itself:

```
click deja "/deja"
click stats "/stats"
```

GitHub ignores those lines; the UI gets navigation from the same file that
draws the picture.

**Hover.** Hovering a node dims every node and edge not incident to it, by
walking the parsed edge list and toggling a class on mermaid's rendered SVG
node ids.

**Layer toggles.** A checkbox per edge kind. Filtering drops `.mmd` lines whose
label is not enabled, then re-renders. The all-edges view of twenty nodes is a
hairball; this is what makes it readable.

**Status overlay.** Colours each node by container state, reusing
`metrics-systems/api.ts` (`fetchJson`, `containerState`, `containerDisplayName`).
When the metrics API is unreachable every node goes to `unknown` and the page
still draws — a topology diagram that blanks because a dashboard is down has
failed at its one job.

### The node-to-container join

The one real integration risk. Compose service names and container names do not
reliably match — `microgpt-serve` is hyphenated where its siblings are not, and
mcpserver's own env refers to `one-d4` where the service is `one_d4`. The
mapping is an explicit table in `topology.ts` with a unit test, not string
normalization that silently mismatches and reports a live service as unknown.
`ui_` nodes and `s3` are absent from the table by design, and the join treats
absence as "not a container" rather than "unknown".

## Syncing the copy

`scripts/sync-topology.sh` in the UI repo curls the mermaid block out of
MoonBase's `docs/ARCHITECTURE.md` and writes `topology.mmd`. Nothing runs it
automatically, consistent with the no-gate decision above; the UI can lag the
doc until someone runs it.

## Testing

Pure functions in `topology.ts` — parsing edges and kinds, filtering by enabled
kinds, joining node ids to container names — get vitest unit tests. `TopologyPage`
gets RTL tests for three states: loading, metrics-unreachable, and a layer
toggle changing the rendered edge set. Mermaid is mocked; it is a rendering
dependency, not logic worth asserting.

No test lands in MoonBase. The diagram is prose there, and the decision above
was explicitly not to gate it.

## Work split

Two PRs, MoonBase first so the UI has something to copy.

1. **MoonBase:** `docs/ARCHITECTURE.md` with the diagram and the route/jobs
   tables. Link it from `CLAUDE.md`'s docs list.
2. **muchq.com:** `src/apps/topology/`, the `/topology` route, the sync script,
   and the tests.
