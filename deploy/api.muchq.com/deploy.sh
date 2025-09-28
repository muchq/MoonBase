#!/bin/bash
set -e

# Copy deployment files
echo "Copying deployment files..."
scp -r deploy/api.muchq.com/compose.yaml deploy/api.muchq.com/Caddyfile ubuntu@api.muchq.com:~/

# Copy observability compose file from root
echo "Copying observability compose file..."
scp docker-compose.observability.yml ubuntu@api.muchq.com:~/

# Create observability directory structure and copy configs if they exist
echo "Setting up observability configs..."
ssh ubuntu@api.muchq.com "mkdir -p ~/o11y/grafana/dashboards ~/o11y/grafana/datasources"

# Copy observability configs if they exist locally
if [ -d "o11y" ]; then
  echo "Copying observability configuration files..."
  scp -r o11y ubuntu@api.muchq.com:~/
fi

# Check if .env file exists locally and copy it
if [ -f ".env" ]; then
  echo "Copying environment variables..."
  scp .env ubuntu@api.muchq.com:~/
fi

# Pull images and restart services
echo "Pulling images and restarting services..."
ssh ubuntu@api.muchq.com << EOF
  # Export environment variables if .env exists
  if [ -f ".env" ]; then
    export \$(cat .env | grep -v '^#' | xargs)
  fi
  
  # Create the shared network if it doesn't exist
  sudo docker network create muchq_network 2>/dev/null || true
  
  sudo docker compose -f compose.yaml -f docker-compose.observability.yml pull
  sudo docker compose -f compose.yaml -f docker-compose.observability.yml up -d --remove-orphans

  # Reload Caddy configuration
  sudo docker compose exec caddy caddy reload --config /etc/caddy/Caddyfile
EOF

echo "Deployment complete!"

