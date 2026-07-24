#!/bin/bash
set -e

HOST=ubuntu@consolidated.cmptr.info
COMPOSE_FILE=deploy/consolidated/compose.yaml

usage() {
  cat <<'USAGE'
Usage: deploy.sh [OPTIONS]

Deploys the consolidated stack. Every image is pinned to a commit SHA, so what
runs is reproducible and a rollback is just --sha.

Options:
  -l, --list [COUNT]   Show the last COUNT commits (default 10) and exit
  -s, --sha SHA        Deploy a specific commit; default is the latest on main
      --latest         Deploy the latest commit on main (the default)
  -y, --yes            Skip the confirmation prompt
  -h, --help           Show this help

Images are published per-commit by .github/workflows/publish.yml, so a commit
can only be deployed once its build has finished. The images are verified to
exist before anything on the host is touched.
USAGE
}

LIST_COUNT=""
TARGET_REF=""
ASSUME_YES=0

while [ $# -gt 0 ]; do
  case "$1" in
    -l | --list)
      LIST_COUNT=10
      case "${2:-}" in
        [0-9]*)
          LIST_COUNT=$2
          shift
          ;;
      esac
      shift
      ;;
    -s | --sha)
      if [ -z "${2:-}" ]; then
        echo "Error: --sha needs a commit" >&2
        exit 1
      fi
      TARGET_REF=$2
      shift 2
      ;;
    --latest)
      TARGET_REF=""
      shift
      ;;
    -y | --yes)
      ASSUME_YES=1
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

# Deploy targets are commits on main that CI has already published, so resolve
# against the remote rather than whatever the local checkout happens to be on.
git fetch --quiet origin main

# Best-effort: the host records what it last deployed, which labels the table
# and gives the confirmation prompt a before/after.
deployed_sha=$(ssh -o BatchMode=yes -o ConnectTimeout=5 "$HOST" \
  "grep -s '^DEPLOY_SHA=' ~/.env | cut -d= -f2" 2>/dev/null || true)

describe() { # describe <sha> -> "subject", or a placeholder when not local
  git log -1 --pretty=%s "$1" 2>/dev/null || echo "(unknown commit)"
}

if [ -n "$LIST_COUNT" ]; then
  printf '%-9s  %-10s  %s\n' "COMMIT" "DATE" "SUBJECT"
  git log --max-count="$LIST_COUNT" --date=short \
    --pretty=tformat:'%h|%ad|%s' origin/main |
    while IFS='|' read -r sha date subject; do
      marker=""
      case "$deployed_sha" in
        "$sha"*) marker="  <- deployed" ;;
      esac
      printf '%-9s  %-10s  %s%s\n' "$sha" "$date" "$subject" "$marker"
    done
  exit 0
fi

if [ -n "$TARGET_REF" ]; then
  DEPLOY_SHA=$(git rev-parse --verify "${TARGET_REF}^{commit}" 2>/dev/null) || {
    echo "Error: '$TARGET_REF' is not a commit in this repo" >&2
    exit 1
  }
else
  DEPLOY_SHA=$(git rev-parse --verify "origin/main^{commit}")
fi
export DEPLOY_SHA
short_sha=$(git rev-parse --short "$DEPLOY_SHA")

echo "Deploying $short_sha  $(describe "$DEPLOY_SHA")"
if [ -n "$deployed_sha" ] && [ "$deployed_sha" != "$DEPLOY_SHA" ]; then
  echo "Replacing $(git rev-parse --short "$deployed_sha" 2>/dev/null || echo "$deployed_sha")  $(describe "$deployed_sha")"
elif [ "$deployed_sha" = "$DEPLOY_SHA" ]; then
  echo "(already the deployed revision)"
fi

if [ "$ASSUME_YES" -ne 1 ]; then
  printf 'Proceed? [y/N] '
  read -r reply
  case "$reply" in
    [yY] | [yY][eE][sS]) ;;
    *)
      echo "Aborted."
      exit 1
      ;;
  esac
fi

# Fail before touching the host: a SHA whose publish build is still running (or
# failed) has no images, and finding that out at `compose pull` would leave the
# host half-updated.
images=$(grep -o 'ghcr\.io/muchq/[a-z0-9_-]*' "$COMPOSE_FILE" | sort -u)
echo "Verifying published images for $short_sha..."
missing=$(ssh "$HOST" "for img in $images; do sudo docker manifest inspect \$img:$DEPLOY_SHA >/dev/null 2>&1 || echo \$img; done")
if [ -n "$missing" ]; then
  echo "Error: no image published at $short_sha for:" >&2
  echo "$missing" | sed 's/^/  /' >&2
  echo "Its publish build may still be running or may have failed." >&2
  exit 1
fi

# Copy deployment files
echo "Copying deployment files..."
scp -r deploy/consolidated/compose.yaml deploy/consolidated/Caddyfile deploy/consolidated/docker-compose.observability.yml "$HOST":~/

# Copy r3dr static assets
echo "Copying r3dr static assets..."
scp -r domains/r3dr/apps/r3dr_web/* "$HOST":~/r3dr-assets/

# Create observability directory structure and copy configs if they exist
echo "Setting up observability configs..."
ssh "$HOST" "mkdir -p ~/o11y"

# Copy observability configs
echo "Copying observability configuration files..."
scp -r deploy/consolidated/o11y/* "$HOST":~/o11y/

# Copy Forgejo configuration
echo "Copying Forgejo configuration..."
scp -r deploy/consolidated/forgejo/app.ini "$HOST":~/forgejo-app.ini

# Check if .env file exists locally and copy it
if [ -f ".env" ]; then
  echo "Copying environment variables..."
  scp .env "$HOST":~/
fi

# Pull images and restart services
echo "Pulling images and restarting services..."
ssh "$HOST" << EOF
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

  # Record the revision being deployed. compose.yaml reads DEPLOY_SHA to pin
  # every image tag, so this file is what the stack comes back up on after an
  # unattended \`compose up\` (a reboot, a manual restart).
  touch ~/.env
  grep -v '^DEPLOY_SHA=' ~/.env > ~/.env.tmp || true
  echo "DEPLOY_SHA=${DEPLOY_SHA}" >> ~/.env.tmp
  mv ~/.env.tmp ~/.env
  export DEPLOY_SHA=${DEPLOY_SHA}

  sudo -E docker compose -f compose.yaml -f docker-compose.observability.yml pull
  sudo -E docker compose -f compose.yaml -f docker-compose.observability.yml up -d --remove-orphans

  # Reload Caddy configuration. Target the admin API on IPv4 explicitly: inside
  # the container \`localhost\` resolves to ::1 first, but Caddy's admin endpoint
  # listens only on 127.0.0.1:2019, so the default localhost reload is refused.
  sudo docker compose exec caddy caddy reload --config /etc/caddy/Caddyfile --address 127.0.0.1:2019
EOF

echo "Deployment complete! Running $short_sha"
