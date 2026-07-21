#!/bin/bash
set -e

# This script is meant to be run from the MoonBase directory
# It sets up a local docker deployment similar to the production deploy.sh

# Check if we're in the MoonBase directory (bzlmod project)
if [ ! -f "MODULE.bazel" ]; then
  echo "Error: This script must be run from the MoonBase directory (MODULE.bazel not found)"
  exit 1
fi

# Create local_docker directory if it doesn't exist
echo "Setting up local_docker directory..."
mkdir -p local_docker

# Copy deployment files
echo "Copying deployment files..."
cp deploy/consolidated/compose.yaml local_docker/
cp deploy/consolidated/Caddyfile local_docker/
cp deploy/consolidated/docker-compose.observability.yml local_docker/

# Copy r3dr static assets
echo "Copying r3dr static assets..."
mkdir -p local_docker/r3dr-assets
cp -r domains/r3dr/apps/r3dr_web/* local_docker/r3dr-assets/

# Create observability directory structure and copy configs
echo "Setting up observability configs..."
mkdir -p local_docker/o11y
cp -r deploy/consolidated/o11y/* local_docker/o11y/


# Copy .env file if it exists
if [ -f ".env" ]; then
  echo "Copying environment variables..."
  cp .env local_docker/
fi

# Change to local_docker directory
cd local_docker

# Export environment variables if .env exists
if [ -f ".env" ]; then
  echo "Loading environment variables..."
  export $(cat .env | grep -v '^#' | xargs)
fi

# Ensure the shared network exists with the pinned subnet AND an ip-range that
# keeps the dynamic pool off Caddy's static 172.28.0.2 (smithy-cpp ADR-0012).
# The observability compose file marks this network `external: true`, so Compose
# won't create it and ignores the ipam block — we own it here.
echo "Ensuring docker network..."
if ! docker network inspect muchq_network >/dev/null 2>&1; then
  docker network create --subnet 172.28.0.0/16 --ip-range 172.28.1.0/24 --gateway 172.28.0.1 muchq_network
elif ! docker network inspect muchq_network \
      --format '{{range .IPAM.Config}}{{.Subnet}}|{{.IPRange}}{{end}}' | grep -q '172.28.0.0/16|172.28.1.0/24'; then
  echo "muchq_network has the wrong subnet/ip-range; recreating..."
  docker compose -f compose.yaml -f docker-compose.observability.yml down
  docker network rm muchq_network
  docker network create --subnet 172.28.0.0/16 --ip-range 172.28.1.0/24 --gateway 172.28.0.1 muchq_network
fi

# Pull only the published images (skip prom_proxy for now)
echo "Pulling published images..."
docker pull ghcr.io/muchq/games_ws_backend:latest || echo "Warning: Failed to pull games_ws_backend"
docker pull ghcr.io/muchq/portrait:latest || echo "Warning: Failed to pull portrait" 
docker pull caddy:2-alpine
docker pull prom/prometheus:v3.5.0
docker pull otel/opentelemetry-collector-contrib:0.133.0

# Start services (prom_proxy will need to be built locally or skipped)
echo "Starting services..."
docker compose -f compose.yaml -f docker-compose.observability.yml up -d --remove-orphans

echo "Local deployment complete!"
echo "Services should be accessible at:"
echo "- Main application: http://localhost"
echo "- Prometheus: http://localhost:9090"
echo ""
echo "To stop the services, run:"
echo "  cd local_docker && docker compose -f compose.yaml -f docker-compose.observability.yml down"