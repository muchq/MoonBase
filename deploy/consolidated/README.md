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

- **r3dr.net** - URL shortener service
  - [`r3dr`](../../domains/r3dr/apis/r3dr) (port 8085)
  - Static assets served from `/var/www/r3dr`

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
./deploy/consolidated/initialize_host.sh /path/to/db_config
```

This script will:
1. Update system packages
2. Install Docker and Docker Compose
3. Create necessary directories (`/etc/r3dr`)
4. Copy the database config file to `/etc/r3dr/db_config`

**Requirements:**
- SSH access to the host configured (e.g., `ssh ubuntu@consolidated.cmptr.info`)
- Database config file for r3dr service

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
3. Copy r3dr static assets
4. Copy observability configuration
5. Pull the images for that commit
6. Restart the affected services

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
portrait            Ray-traced scene renderer
...
```

Descriptions come from each service's `com.muchq.description` label in
`compose.yaml`, so that file stays the source of truth (the labels also land on
the containers).

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
`o11y/*`, and r3dr's static assets — is always copied from your working tree,
so it stays at whatever you have checked out. Deploy from a clean checkout of
`main`, and be aware that rolling images back past a config change (a new env
var, a new route) leaves the newer config in front of an older binary.

Rolling back further than the newest service is also rejected: the pre-flight
check requires an image at that commit for every service, and a service that
didn't exist yet has none. Use `--service` to roll back one service past that
point.

## Configuration Requirements

Services require configuration files in their respective `/etc` directories on the host:

- `/etc/r3dr/db_config` - Database connection string for r3dr
- `/etc/games_ws_backend/` - Games backend configuration
- `/etc/portrait/` - Portrait service configuration
- `/etc/prom_proxy/` - Prometheus proxy configuration
- `/etc/mithril/` - Mithril service configuration
- `/etc/posterize/` - Posterize service configuration
- `/etc/mcpserver/` - MCP server configuration
- `/etc/microgpt-serve/` - microgpt config, including the model in `model/`
- `/etc/one_d4/` - one_d4 service configuration

## Network

All services run on the `muchq_network` Docker bridge network.
