# Consolidated Deployment

This directory contains the consolidated deployment configuration for multiple services running on a single host.

## Services Deployed

- **api.muchq.com** - API backend services
  - [`games_ws_backend`](../../domains/games/apis/games_ws_backend) (port 8080)
  - [`portrait`](../../domains/graphics/apis/portrait) (port 8081)
  - [`prom_proxy`](../../domains/platform/apis/prom_proxy) (port 8082)
  - [`mithril`](../../domains/games/apis/mithril) (port 8083)
  - [`posterize`](../../domains/graphics/apis/posterize) (port 8084)
  - [`mcpserver`](../../domains/games/apis/mcpserver) (port 8086)

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

To deploy or update services:

```bash
./deploy/consolidated/deploy.sh
```

This will:
1. Copy deployment files to the host
2. Copy r3dr static assets
3. Copy observability configuration
4. Pull latest Docker images
5. Restart all services

## Configuration Requirements

Services require configuration files in their respective `/etc` directories on the host:

- `/etc/r3dr/db_config` - Database connection string for r3dr
- `/etc/games_ws_backend/` - Games backend configuration
- `/etc/portrait/` - Portrait service configuration
- `/etc/prom_proxy/` - Prometheus proxy configuration
- `/etc/mithril/` - Mithril service configuration
- `/etc/posterize/` - Posterize service configuration
- `/etc/mcpserver/` - MCP server configuration

## Network

All services run on the `muchq_network` Docker bridge network.
