#!/bin/bash
# daily-restart.sh – pull latest images and restart the consolidated stack.
#
# Install as a cron job to run daily at midnight server time:
#   sudo crontab -e
#   0 0 * * * /path/to/daily-restart.sh >> /var/log/daily-restart.log 2>&1
#
# The script must be run from the directory containing compose.yaml, or
# set COMPOSE_DIR below to the absolute path of that directory.

set -euo pipefail

COMPOSE_DIR="${COMPOSE_DIR:-$(dirname "$(realpath "$0")")}"
COMPOSE_FILES=(-f "$COMPOSE_DIR/compose.yaml" -f "$COMPOSE_DIR/docker-compose.observability.yml")
LOG_PREFIX="[daily-restart $(date -u '+%Y-%m-%dT%H:%M:%SZ')]"

cd "$COMPOSE_DIR"

echo "$LOG_PREFIX Starting daily image pull and restart"

# Load environment variables if present
if [ -f "$COMPOSE_DIR/.env" ]; then
  # shellcheck disable=SC2046
  export $(grep -v '^#' "$COMPOSE_DIR/.env" | xargs)
fi

echo "$LOG_PREFIX Pulling latest images..."
sudo docker compose "${COMPOSE_FILES[@]}" pull

echo "$LOG_PREFIX Restarting services..."
sudo docker compose "${COMPOSE_FILES[@]}" up -d --remove-orphans

echo "$LOG_PREFIX Reloading Caddy configuration..."
sudo docker compose "${COMPOSE_FILES[@]}" exec caddy caddy reload --config /etc/caddy/Caddyfile || true

echo "$LOG_PREFIX Done"
