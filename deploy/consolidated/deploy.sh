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

  # Create the shared network if it doesn't exist
  sudo docker network create muchq_network 2>/dev/null || true

  sudo docker compose -f compose.yaml -f docker-compose.observability.yml pull
  sudo docker compose -f compose.yaml -f docker-compose.observability.yml up -d --remove-orphans

  # Reload Caddy configuration
  sudo docker compose exec caddy caddy reload --config /etc/caddy/Caddyfile
EOF

echo "Deployment complete!"

