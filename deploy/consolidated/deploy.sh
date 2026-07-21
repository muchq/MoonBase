#!/bin/bash
set -e

# Copy deployment files
echo "Copying deployment files..."
scp -r deploy/consolidated/compose.yaml deploy/consolidated/Caddyfile deploy/consolidated/docker-compose.observability.yml ubuntu@consolidated.cmptr.info:~/

# Copy r3dr static assets
echo "Copying r3dr static assets..."
scp -r domains/r3dr/apps/r3dr_web/* ubuntu@consolidated.cmptr.info:~/r3dr-assets/

# Create observability directory structure and copy configs if they exist
echo "Setting up observability configs..."
ssh ubuntu@consolidated.cmptr.info "mkdir -p ~/o11y"

# Copy observability configs
echo "Copying observability configuration files..."
scp -r deploy/consolidated/o11y/* ubuntu@consolidated.cmptr.info:~/o11y/

# Copy Forgejo configuration
echo "Copying Forgejo configuration..."
scp -r deploy/consolidated/forgejo/app.ini ubuntu@consolidated.cmptr.info:~/forgejo-app.ini


# Check if .env file exists locally and copy it
if [ -f ".env" ]; then
  echo "Copying environment variables..."
  scp .env ubuntu@consolidated.cmptr.info:~/
fi

# Pull images and restart services
echo "Pulling images and restarting services..."
ssh ubuntu@consolidated.cmptr.info << EOF
  # Export environment variables if .env exists
  if [ -f ".env" ]; then
    export \$(cat .env | grep -v '^#' | xargs)
  fi

  # Move r3dr static assets to web root
  sudo mkdir -p /var/www/r3dr
  sudo cp -r ~/r3dr-assets/* /var/www/r3dr/

  # Set up Forgejo config directory
  sudo mkdir -p /etc/forgejo
  sudo cp ~/forgejo-app.ini /etc/forgejo/app.ini
  sudo chown -R 1000:1000 /etc/forgejo

  # Ensure the shared network exists with the pinned subnet AND an ip-range that
  # keeps the dynamic pool off Caddy's static 172.28.0.2 (smithy-cpp ADR-0012).
  # The observability compose file marks this network \`external: true\`, so
  # Compose won't create it and ignores the ipam block — we own it here.
  if ! sudo docker network inspect muchq_network >/dev/null 2>&1; then
    sudo docker network create --subnet 172.28.0.0/16 --ip-range 172.28.1.0/24 --gateway 172.28.0.1 muchq_network
  elif ! sudo docker network inspect muchq_network \
        --format '{{range .IPAM.Config}}{{.Subnet}}|{{.IPRange}}{{end}}' | grep -q '172.28.0.0/16|172.28.1.0/24'; then
    echo "muchq_network has the wrong subnet/ip-range; recreating..."
    sudo docker compose -f compose.yaml -f docker-compose.observability.yml down
    sudo docker network rm muchq_network
    sudo docker network create --subnet 172.28.0.0/16 --ip-range 172.28.1.0/24 --gateway 172.28.0.1 muchq_network
  fi

  sudo docker compose -f compose.yaml -f docker-compose.observability.yml pull
  sudo docker compose -f compose.yaml -f docker-compose.observability.yml up -d --remove-orphans

  # Reload Caddy configuration. Target the admin API on IPv4 explicitly: inside
  # the container \`localhost\` resolves to ::1 first, but Caddy's admin endpoint
  # listens only on 127.0.0.1:2019, so the default localhost reload is refused.
  sudo docker compose exec caddy caddy reload --config /etc/caddy/Caddyfile --address 127.0.0.1:2019
EOF

echo "Deployment complete!"

