# Consolidated Deployment

This directory contains the consolidated deployment configuration for multiple services running on a single host.

## Services Deployed

`deploy.sh --services` lists the application services below, read from
`compose.yaml`. Caddy and the observability stack are deployed too but aren't
individually targetable.

- **api.muchq.com** - API backend services
  - [`games_ws_backend`](../../domains/games/apis/games_ws_backend) (port 8080)
  - [`portrait`](../../domains/graphics/apis/portrait) (port 8081)
  - [`golf_hub`](../../domains/games/apis/golf_hub) (port 8089, `/games/v2/*`)
  - [`prom_proxy`](../../domains/platform/apis/prom_proxy) (port 8082)
  - [`mithril`](../../domains/games/apis/mithril) (port 8083)
  - [`posterize`](../../domains/graphics/apis/posterize) (port 8084)
  - [`mcpserver`](../../domains/games/apis/mcpserver) (port 8086)
  - [`microgpt-serve`](../../domains/ai/apis/microgpt_serve) (port 8087)
  - [`one_d4`](../../domains/games/apis/one_d4) (port 8088)
  - [`one_d4_v2`](../../domains/games/apis/one_d4_v2) (port 8090)
  - [`iili`](../../domains/iili/apis/iili) (port 8091, `/iili/v1/*`)

- **i.iili.uk** - Short-link redirects (#1359)
  - Caddy rewrites `GET|HEAD /r/{slug}` → `iili` `/iili/v1/r/{slug}`
  - SPA lives on Cloudflare at `iili.uk` (not this host)

- **Observability Stack**
  - Prometheus (port 9090)
  - OpenTelemetry Collector (ports 4318, 8889)
  - cAdvisor (port 8080 internal)

- **Caddy** - Reverse proxy and web server
  - Handles HTTPS certificates automatically
  - Routes requests to appropriate backend services

## Files

- `compose.yaml` - Main Docker Compose configuration for application services
- `Caddyfile` - Caddy reverse proxy configuration
- `docker-compose.observability.yml` - Observability stack configuration
- `deploy.sh` - Deployment script
- `initialize_host.sh` - Script to set up a fresh host

## Initializing a New Host

To set up a fresh Lightsail (or similar) instance:

```bash
./deploy/consolidated/initialize_host.sh
```

This script will:
1. Update system packages
2. Install Docker and Docker Compose
3. Create the Forgejo config directory (`/etc/forgejo`)

**Requirements:**
- SSH access to the host configured (e.g., `ssh ubuntu@consolidated.cmptr.info`)

After initialization, you may need to reboot the instance.

## Deploying

To deploy the latest commit on `main`:

```bash
./deploy/consolidated/deploy.sh
```

It shows the commit you're about to deploy and asks to confirm:

```
Deploying all services at 1694f01  posterize: accept common image formats and preserve them on output (#1207)
Replacing 891973a  server_pal: bundle webpki roots into the OTLP client (#1205)
Proceed? [y/N]
```

Every image is pinned to that commit's SHA (`compose.yaml` reads `DEPLOY_SHA`),
so a deploy is reproducible and the running version is whatever you confirmed.
Images are published per-commit by `publish.yml`, so a commit is only
deployable once its build has finished — `deploy.sh` verifies the images exist
before touching the host.

To see what's available, with the deployed revision marked:

```bash
./deploy/consolidated/deploy.sh --list      # last 10 commits
./deploy/consolidated/deploy.sh --list 25
```

```
COMMIT     DATE        SUBJECT
1694f01    2026-07-24  posterize: accept common image formats and preserve them on output (#1207)
891973a    2026-07-24  server_pal: bundle webpki roots into the OTLP client (#1205)  <- deployed
```

Add `-y` to skip the confirmation. The deploy itself will:
1. Verify every image exists at that commit, before touching the host
2. Copy deployment files to the host
3. Copy observability configuration
4. Pull the images for that commit
5. Restart the affected services

### Deploying one service

To ship a change without restarting the whole stack:

```bash
./deploy/consolidated/deploy.sh --services            # what can be deployed
./deploy/consolidated/deploy.sh --service posterize
```

```
SERVICE             DESCRIPTION
games_ws_backend    Websocket backend for v1 games
golf_hub            Golf v2 hub on smithy event streams (/games/v2/*)
mcpserver           Model Context Protocol server
...
```

Descriptions come from each service's `com.muchq.description` label in
`compose.yaml`, so that file stays the source of truth (the labels also land on
the containers). The listing is the services with a pinned image, which is
exactly what `--service` accepts — a labelled service without one (like
`shared_postgres`) carries its description on the container but is not a deploy
target.

A targeted deploy pins **only** that service, recorded as its own variable
(`POSTERIZE_SHA`) so the pin survives a reboot without dragging the rest of the
stack to a new revision. A full deploy clears every per-service pin, so the
stack converges back on a single revision.

Only the named container is recreated, but config files are still synced and
Caddy is still reloaded, exactly as in a full deploy — "targeted" refers to the
container, not to everything the script touches.

### Checking what's actually running

```bash
./deploy/consolidated/deploy.sh --status
```

```
SERVICE             VERSION    RESTARTS  STATE
caddy               2-alpine   0         Up 3 hours
golf_hub            7faca68    0         Up 3 hours
mithril             1694f01    0         Up 12 minutes
posterize           7faca68    47        Restarting (101) 8 seconds ago
```

This reads the containers themselves rather than the recorded pins, so it shows
the *fact* rather than the intent. It exits non-zero when a container is not up,
or when its revision doesn't match the recorded pin — the latter meaning a
deploy didn't recreate it, which is also how you confirm a deploy took effect.
A full deploy re-converges the stack.

`RESTARTS` is Docker's restart count, which resets when a container is
recreated — so it reads as restarts *since that service was last deployed*. A
climbing count next to a low uptime is a crash loop, and the exit code is in the
state (`Restarting (101)` above is a Rust panic). Docker has no windowed rate;
for restarts-over-time across the fleet, that's the dashboard's job (#1208).

## Rollback

Deploy an earlier commit — pick one with `--list`, then:

```bash
./deploy/consolidated/deploy.sh --sha abc1234
```

The whole stack moves together rather than leaving a hand-edited pin behind.

Note this rolls back **images only**. Config — `compose.yaml`, the `Caddyfile`,
and `o11y/*` — is always copied from your working tree,
so it stays at whatever you have checked out. Deploy from a clean checkout of
`main`, and be aware that rolling images back past a config change (a new env
var, a new route) leaves the newer config in front of an older binary.

Rolling back further than the newest service is also rejected: the pre-flight
check requires an image at that commit for every service, and a service that
didn't exist yet has none. Use `--service` to roll back one service past that
point.

## Configuration Requirements

Services require configuration files in their respective `/etc` directories on the host:

- `/etc/games_ws_backend/` - Games backend configuration
- `/etc/portrait/` - Portrait service configuration
- `/etc/prom_proxy/` - Prometheus proxy configuration
- `/etc/mithril/` - Mithril service configuration
- `/etc/posterize/` - Posterize service configuration
- `/etc/mcpserver/` - MCP server configuration
- `/etc/microgpt-serve/` - microgpt config, including the model in `model/`

one_d4 is deliberately not on that list: its JDBC URL comes from `compose.yaml`, and it needs
nothing from the host filesystem.

### Database URLs

golf_hub, one_d4 and iili take their database URLs from `compose.yaml`, interpolating a
password from the host's `~/.env`: `GOLF_HUB_DB_URL` and `IILI_DB_URL` (libpq form, read from
C++) and `INDEXER_DB_URL` (JDBC form, read by pgjdbc). All point at the `shared_postgres` service.
**`R3DR_V2_DB_PASSWORD`** in `~/.env` (#1359) is the one place the old name survives: `iili_db_init`
provisions the `r3dr_v2` role and database with it on every deploy, idempotently, because renaming
a role and database holding live rows is an operation, not a rename. Keep it URL-safe (no
`@ / ? # %` or quotes): it rides in a libpq URL and a single-quoted SQL literal. Compose
refuses to start the service if it's unset.

Keeping a URL here rather than in a host file is what makes the hostname visible to this repo:
`deploy_config_test.go` fails if a database host is not a Postgres service this file publishes, so
the instance can be renamed (#1225) by editing one file instead of by keeping the old name
resolving forever.

one_d4's credentials are separate variables (`INDEXER_DB_USERNAME`, `INDEXER_DB_PASSWORD`) rather than
query parameters on the URL, because pgjdbc URL-decodes those — see `DataSourceFactory.create`.

**Tradeoff, stated rather than absorbed.** A host file can be mode-restricted; an environment
variable is readable in `docker inspect` output and in `/proc/<pid>/environ` by anyone who can
reach them. golf_hub already accepts that, so this makes one_d4 consistent with the rest of the
stack rather than newly exposed — but the password does move from a root-owned file into container
metadata, and that is a real change in where it sits.

`INDEXER_DB_URL` is one_d4's only source for the URL: it reads no host file, and H2 is a
test-only dependency whose driver the container does not carry. An unset variable is therefore a
container that exits on boot naming the variable, rather than one that starts, serves, and loses
every write on restart. `deploy_config_test.go` fails if this file stops setting it.

When verifying a deploy, read the **body** of `/health` — it answers 200 with
`{"status":"DOWN"}` when Postgres is unreachable, so the status code alone proves nothing.

## Network

All services run on the `muchq_network` Docker bridge network.

## Crawler guards on git.muchq.com

Forgejo runs under a 0.5-CPU cap, and the git-backed pages (`/commit/`,
`/compare/`, `/blame/`, `/commits/`, `/src/`, `/archive/`) fork git per
request, so a crawler walking them starves the site (#1447). The
`git.muchq.com` block answers 403 to the `meta-externalagent` User-Agent, to
`57.141.0.0/16`, and to any self-identified crawler on those six routes.
`/issues/` and `/raw/` stay open — a database read and a file read, and worth
more as indexable pages than as saved CPU.

Three things to know before editing it:

- **robots.txt is not the lever.** Forgejo already serves one disallowing
  `/*/*/src/` and `/user/`, and the crawler requests both anyway. That is why
  the guard is in Caddy.
- **The User-Agent and subnet guards are redundant on purpose.** The agent
  self-identifies today and may stop; the range is Meta's today and may move.
- **The expensive-route matcher ANDs its User-Agent and path conditions** in
  one matcher block. Split into two it becomes an OR, wrong in both directions
  at once: crawlers lose the cheap pages they are welcome to, and humans lose
  `/commit/` and `/blame/`. `deploy_config_test.go` fails if they are
  separated.

Only HTTP is affected. Git over SSH reaches Forgejo on host port 222 without
passing through Caddy.

## The shared database

`shared_postgres` is the one Postgres instance on the host. `one_d4`,
`golf_hub` and `iili` each keep a database on it; `golf_hub_db_init` and
`iili_db_init` provision their roles and databases on every deploy
(idempotently — the `docker-entrypoint-initdb.d` hook only fires on a
fresh volume).

`one_d4_migrate` is the same one-shot shape for one_d4's schema (#1419): it
applies the numbered `.sql` files in
[`one_d4/migrations/`](../../domains/games/apis/one_d4/migrations/) and
exits. Both `one_d4` and `one_d4_worker` gate on it with
`service_completed_successfully` — the worker so it no longer waits for the
Java service to boot, the service so its own boot-time migration (which
still runs, until #1426 demotes it to a verifier) is serialized behind this
one rather than racing it.

Two things about `shared_postgres` are load-bearing and easy to undo by
accident:

- **The volume key is not the volume's name.** Compose prefixes it with the
  project, so `shared_pgdata` is `ubuntu_shared_pgdata` here and
  `local_docker_shared_pgdata` under `local_deploy.sh`. Renaming the key in
  `compose.yaml` alone does not rename either volume — it mounts an empty one,
  initdb fills it, and the stack comes up green and blank. Renaming means
  copying the volume on the host in the same operation. Pinning `name:` instead
  is not the way out: it hardcodes one project's prefix, and `ubuntu` is only
  the host's login account.
- **The bootstrap database, role and `pg_isready` user are `one_d4`.** initdb
  wrote them on first boot; `POSTGRES_*` is ignored on an initialized cluster,
  so changing them here does nothing. That needs a migration.

Renaming the volume, for reference — the copy and the deploy are one operation,
or writes land in the volume you are copying away from:

```bash
sudo docker compose -f compose.yaml -f docker-compose.observability.yml stop
sudo docker volume create ubuntu_shared_pgdata
sudo docker run --rm -v ubuntu_one_d4_pgdata:/from -v ubuntu_shared_pgdata:/to \
  alpine sh -c '
    if [ -n "$(ls -A /to 2>/dev/null)" ]; then
      echo "refusing: /to is not empty (partial or previous run?)" >&2
      exit 1
    fi
    cd /from && cp -a . /to
  '
# then deploy the compose.yaml carrying the new key
```

`docker volume create` is a no-op when the volume already exists, and `cp -a . /to` merges into
whatever is already there rather than replacing it — safe the first time, but re-running after a
partial or previously-started copy would merge files from two cluster states into one directory.
The check above refuses unless the destination is empty; if you need to retry after a partial copy,
remove and recreate the destination volume explicitly first.

The old volume is left in place, so rollback is the previous `compose.yaml` and
a redeploy — see **Rollback** above about config always coming from your working
tree. Drop it once the new one has soaked.
