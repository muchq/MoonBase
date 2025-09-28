#!/bin/bash
set -e

# Copy deployment files
echo "Copying deployment files..."
scp -r deploy/r3dr.net/compose.yaml deploy/r3dr.net/Caddyfile ubuntu@r3dr.net:~/

# Copy static assets to home directory first
echo "Copying static assets..."
scp -r web/r3dr/* ubuntu@r3dr.net:~/r3dr-assets/

# Pull images and restart services
echo "Pulling images and restarting services..."
ssh ubuntu@r3dr.net << EOF
  # Move static assets to web root
  sudo cp -r ~/r3dr-assets/* /var/www/r3dr/

  # Pull latest Docker image
  sudo docker compose -f compose.yaml pull

  # Restart r3dr service
  sudo docker compose -f compose.yaml up -d --remove-orphans

  # Reload Caddy configuration (Caddy is running directly on host, not in Docker)
  sudo caddy reload --config /home/ubuntu/Caddyfile
EOF

echo "r3dr deployment complete!"