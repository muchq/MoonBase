#!/bin/bash
set -e

# Copy deployment files
echo "Copying deployment files..."
scp -r deploy/consolidated/compose.yaml deploy/consolidated/Caddyfile ubuntu@consolidated.cmptr.info:~/

# Copy r3dr static assets
echo "Copying r3dr static assets..."
scp -r web/r3dr/* ubuntu@consolidated.cmptr.info:~/r3dr-assets/

# Copy observability compose file from root
echo "Copying observability compose file..."
scp docker-compose.observability.yml ubuntu@consolidated.cmptr.info:~/

# Create observability directory structure and copy configs if they exist
echo "Setting up observability configs..."
ssh ubuntu@consolidated.cmptr.info "mkdir -p ~/o11y/grafana/dashboards ~/o11y/grafana/datasources"

# Copy observability configs if they exist locally
if [ -d "o11y" ]; then
  echo "Copying observability configuration files..."
  scp -r o11y ubuntu@consolidated.cmptr.info:~/
fi

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

  # Create the shared network if it doesn't exist
  sudo docker network create muchq_network 2>/dev/null || true

  sudo docker compose -f compose.yaml -f docker-compose.observability.yml pull
  sudo docker compose -f compose.yaml -f docker-compose.observability.yml up -d --remove-orphans

  # Reload Caddy configuration
  sudo docker compose exec caddy caddy reload --config /etc/caddy/Caddyfile
EOF

echo "Deployment complete!"

