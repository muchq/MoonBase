# System and Dataflow

Every container in the consolidated deployment, every public name that reaches
one, and how data moves between them.

Hand-maintained. Nothing checks it against `deploy/consolidated/compose.yaml`,
so a service added there is a service missing here until somebody adds it.

Node ids are compose service names. `ui_*` nodes are muchq.com routes, not
containers — they exist because several services are only ever reached through
a page whose name is different from theirs.

```mermaid
flowchart LR
  subgraph dns["Public names"]
    muchq_com["muchq.com"]
    iili_uk["iili.uk"]
    api_muchq["api.muchq.com"]
    i_iili_uk["i.iili.uk"]
    gpt_muchq["gpt.muchq.com"]
    git_muchq["git.muchq.com"]
    api_1d4["api.1d4.net"]
    mcp_1d4["mcp.1d4.net"]
    cmptr["consolidated.cmptr.info"]
  end

  subgraph ui["muchq.com routes (Cloudflare)"]
    ui_games["/games"]
    ui_tracy["/tracy"]
    ui_posterize["/posterize"]
    ui_wordchains["/wordchains"]
    ui_iili["/iili"]
    ui_stats["/stats"]
    ui_deja["/deja"]
    ui_metrics["/metrics"]
  end

  subgraph edge["Host edge"]
    caddy["caddy"]
  end

  subgraph apps["Applications"]
    games_hub["games_hub"]
    one_d4["one_d4"]
    one_d4_worker["one_d4_worker"]
    one_d4_v2["one_d4_v2"]
    mcpserver["mcpserver"]
    portrait["portrait"]
    mithril["mithril"]
    posterize["posterize"]
    microgpt["microgpt-serve"]
    iili["iili"]
    deja["deja"]
    stats["stats"]:::gated
    log_shipper["log_shipper"]:::gated
    forgejo["forgejo"]
    prom_proxy["prom_proxy"]
  end

  subgraph data["Data"]
    shared_postgres[("shared_postgres")]
    s3[("S3")]
  end

  subgraph obs["Observability"]
    otelcol["otelcol"]
    prometheus["prometheus"]
  end

  muchq_com -->|ui| ui_games
  muchq_com -->|ui| ui_tracy
  muchq_com -->|ui| ui_posterize
  muchq_com -->|ui| ui_wordchains
  muchq_com -->|ui| ui_iili
  muchq_com -->|ui| ui_stats
  muchq_com -->|ui| ui_deja
  muchq_com -->|ui| ui_metrics

  ui_games -->|http| api_muchq
  ui_tracy -->|http| api_muchq
  ui_posterize -->|http| api_muchq
  ui_wordchains -->|http| api_muchq
  ui_iili -->|http| api_muchq
  ui_stats -->|http| api_muchq
  ui_deja -->|http| api_muchq
  ui_metrics -->|http| api_muchq

  iili_uk -->|ui| ui_iili

  api_muchq -->|http| caddy
  i_iili_uk -->|http| caddy
  gpt_muchq -->|http| caddy
  git_muchq -->|http| caddy
  api_1d4 -->|http| caddy
  mcp_1d4 -->|http| caddy
  cmptr -->|http| caddy

  caddy -->|http| games_hub
  caddy -->|http| portrait
  caddy -->|http| mithril
  caddy -->|http| posterize
  caddy -->|http| microgpt
  caddy -->|http| one_d4
  caddy -->|http| one_d4_v2
  caddy -->|http| iili
  caddy -->|http| stats
  caddy -->|http| deja
  caddy -->|http| mcpserver
  caddy -->|http| prom_proxy
  caddy -->|http| forgejo

  mcpserver -->|http| one_d4
  mcpserver -->|http| one_d4_v2
  deja -->|events| games_hub

  games_hub -->|sql| shared_postgres
  one_d4 -->|sql| shared_postgres
  one_d4_worker -->|sql| shared_postgres
  iili -->|sql| shared_postgres
  stats -->|sql| shared_postgres

  caddy -.->|logs| deja
  caddy -.->|logs| log_shipper
  one_d4 -.->|logs| log_shipper
  log_shipper -.->|logs| s3
  s3 -.->|logs| stats

  games_hub -.->|metrics| otelcol
  one_d4 -.->|metrics| otelcol
  one_d4_worker -.->|metrics| otelcol
  one_d4_v2 -.->|metrics| otelcol
  mcpserver -.->|metrics| otelcol
  portrait -.->|metrics| otelcol
  mithril -.->|metrics| otelcol
  posterize -.->|metrics| otelcol
  microgpt -.->|metrics| otelcol
  iili -.->|metrics| otelcol
  deja -.->|metrics| otelcol
  otelcol -.->|metrics| prometheus
  prometheus -.->|metrics| prom_proxy

  classDef gated stroke-dasharray: 5 5

  click games_hub "/games"
  click portrait "/tracy"
  click posterize "/posterize"
  click mithril "/wordchains"
  click iili "/iili"
  click stats "/stats"
  click deja "/deja"
  click prom_proxy "/metrics"
```

Arrows point the way data moves, which is not always the way the call goes:
`games_hub` holds `DEJA_URL` and calls `deja`, but what crosses the wire is
deja's prediction, and the lobby is what displays it.

Dashed node borders mark services behind the `stats` compose profile. They do
not start on a plain `docker compose up -d`, because both need S3 credentials
and would otherwise crash-loop.

## Public names

| Name | Served by | Reaches |
| --- | --- | --- |
| `muchq.com` | Cloudflare Workers | the SPA; its routes call `api.muchq.com` |
| `iili.uk` | Cloudflare Workers | the iili SPA |
| `api.muchq.com` | caddy | games_hub, portrait, prom_proxy, mithril, posterize, microgpt-serve, one_d4, one_d4_v2, iili, stats, deja |
| `i.iili.uk` | caddy | iili — `GET /r/*` short-link redirects only |
| `gpt.muchq.com` | caddy | microgpt-serve |
| `git.muchq.com` | caddy | forgejo |
| `api.1d4.net` | caddy | one_d4, stats |
| `mcp.1d4.net` | caddy | mcpserver — `POST /mcp` |
| `consolidated.cmptr.info` | caddy | nothing; static placeholder response |

## Routes on api.muchq.com

| Path | Backend |
| --- | --- |
| `/games/v2/session`, `/games/v2/play` | games_hub |
| `/portrait/v1/trace` | portrait |
| `/metrics/v1/*` | prom_proxy |
| `/mithril/v1/wordchain` | mithril |
| `/imagine/v1/blur`, `/imagine/v1/edges` | posterize |
| `/microgpt/v1/generate`, `/microgpt/v1/chat` | microgpt-serve |
| `/1d4/v1/health`, `/1d4/v1/index`, `/1d4/v1/index/*`, `/1d4/v1/query` | one_d4 |
| `/v2/analyze` | one_d4_v2 |
| `/iili/v1/shorten`, `/iili/v1/r/*` | iili |
| `/stats/v1/*` | stats |
| `/deja/v1/*`, `/deja/v1/next` | deja |

## The log path

The one route through the system that no config file states in one place:

```
caddy writes /var/log/caddy/access.log
  ├── deja tails it read-only, predicting the next request
  └── log_shipper rolls it to S3 and deletes the rolled file
        └── stats reads S3, aggregates into shared_postgres, serves /stats/v1
```

`one_d4` query events ride the same shipper under their own S3 partition.
Rolled files are deleted after upload, which is what keeps the host disk
bounded once shipping owns retention.

## One-shot jobs

Not in the diagram: they order a deploy rather than carry data.

| Job | Runs before |
| --- | --- |
| `games_hub_db_init` | games_hub |
| `iili_db_init` | iili |
| `one_d4_migrate` | one_d4 |
| `stats_db_init` | stats |
