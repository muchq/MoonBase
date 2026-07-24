#!/bin/bash
set -e

# Paths below are repo-relative; self-locate so this works from any directory.
cd "$(dirname "$0")/../.." || exit 1

HOST=ubuntu@consolidated.cmptr.info
COMPOSE_FILE=deploy/consolidated/compose.yaml

usage() {
  cat <<'USAGE'
Usage: deploy.sh [OPTIONS]

Deploys the consolidated stack. Every service image is pinned to a commit SHA,
so what runs is reproducible and a rollback is just --sha.

Options:
  -l, --list [COUNT]     Show the last COUNT commits (default 10) and exit
      --services         Show the deployable services and exit
      --status           Show what each container is actually running, and exit
  -s, --sha SHA          Deploy a specific commit; default is the latest on main
      --latest           Deploy the latest commit on main (the default)
      --service NAME     Deploy only NAME, leaving the rest of the stack alone
  -y, --yes              Skip the confirmation prompt
  -h, --help             Show this help

Images are published per-commit by .github/workflows/publish.yml, so a commit
can only be deployed once its build has finished. The images are verified to
exist before anything on the host is touched.

A full deploy moves every service to one revision and clears any per-service
pins. --service pins just that service, so a hotfix survives a reboot without
dragging the rest of the stack along.
USAGE
}

# The services that carry a pinned image, from the image lines themselves so
# this can't drift from what compose actually deploys.
service_names() {
  grep -o 'ghcr\.io/muchq/[a-z0-9_-]*' "$COMPOSE_FILE" | sed 's|.*/||' | sort -u
}

# Every pin variable compose.yaml reads, taken from the file that consumes
# them. Load-bearing: these are the vars a deploy rewrites on the host.
pin_vars() {
  grep -o '\${[A-Z0-9_]*_SHA' "$COMPOSE_FILE" | tr -d '${' | sort -u
}

# Descriptions for --services. Display only — a missing or reformatted label
# costs a row in the listing, it can't affect what gets deployed.
service_table() {
  awk '
    /^services:/ { in_services = 1; next }
    /^[a-z]/     { in_services = 0 }
    in_services && /^  [a-z0-9_-]+:$/ { svc = $1; sub(/:$/, "", svc); next }
    in_services && /com\.muchq\.description:/ {
      sub(/^ *com\.muchq\.description: */, "")
      gsub(/^"|"$/, "")
      if (svc != "") print svc "|" $0
      svc = ""
    }
  ' "$COMPOSE_FILE"
}

# golf_hub -> GOLF_HUB_SHA, microgpt-serve -> MICROGPT_SERVE_SHA
sha_var_for() {
  local name=${1//-/_}
  echo "${name^^}_SHA"
}

describe() { # describe <sha> -> "subject", or a placeholder when not local
  git log -1 --pretty=%s "$1" 2>/dev/null || echo "(unknown commit)"
}

LIST_COUNT=""
LIST_SERVICES=0
SHOW_STATUS=0
TARGET_REF=""
TARGET_SERVICE=""
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
    --services)
      LIST_SERVICES=1
      shift
      ;;
    --status)
      SHOW_STATUS=1
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
    --service)
      if [ -z "${2:-}" ]; then
        echo "Error: --service needs a service name" >&2
        exit 1
      fi
      TARGET_SERVICE=$2
      shift 2
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

if [ "$LIST_SERVICES" -eq 1 ]; then
  printf '%-18s  %s\n' "SERVICE" "DESCRIPTION"
  service_table | while IFS='|' read -r svc desc; do
    printf '%-18s  %s\n' "$svc" "$desc"
  done
  if [ "$(service_table | wc -l)" -ne "$(service_names | wc -l)" ]; then
    echo "(note: some services have no com.muchq.description label)" >&2
  fi
  exit 0
fi

if [ -n "$TARGET_SERVICE" ] && ! service_names | grep -qx "$TARGET_SERVICE"; then
  echo "Error: unknown service '$TARGET_SERVICE'" >&2
  echo "Run 'deploy.sh --services' to see what's deployable." >&2
  exit 1
fi

# One round trip; the host's .env is both the pin store and the secrets file.
host_env=$(ssh -o BatchMode=yes -o ConnectTimeout=5 "$HOST" 'cat ~/.env' 2>/dev/null || true)
host_pin() { printf '%s\n' "$host_env" | sed -n "s/^$1=//p" | tail -1; }

# What the host records is only the intent; the containers are the fact. They
# diverge when a container wasn't recreated, so read them rather than infer.
if [ "$SHOW_STATUS" -eq 1 ]; then
  # Two views in one connection: ps for the human status string (which already
  # carries the exit code, e.g. "Restarting (101)"), inspect for the restart
  # count, which ps can't report. RestartCount resets when a container is
  # recreated, so it reads as restarts since the last deploy of that service.
  running=$(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST" "
    sudo docker ps -a --filter label=com.docker.compose.service \
      --format 'S|{{.Label \"com.docker.compose.service\"}}|{{.Image}}|{{.Status}}'
    sudo docker ps -aq --filter label=com.docker.compose.service |
      xargs -r sudo docker inspect \
        --format 'R|{{index .Config.Labels \"com.docker.compose.service\"}}|{{.RestartCount}}'
  " 2>/dev/null)
  if [ -z "$running" ]; then
    echo "Error: could not read container state from $HOST" >&2
    exit 1
  fi

  declare -A restarts=()
  while IFS='|' read -r kind svc count; do
    [ "$kind" = "R" ] && [ -n "$svc" ] && restarts[$svc]=$count
  done <<< "$running"

  printf '%-18s  %-9s  %-8s  %s\n' "SERVICE" "VERSION" "RESTARTS" "STATE"
  drifted=0
  unhealthy=0
  while IFS='|' read -r kind svc image state; do
    [ "$kind" = "S" ] || continue
    [ -n "$svc" ] || continue
    tag=${image##*:}
    shown=$tag
    case "$tag" in
      # Show a revision the way --list does; leave third-party tags alone.
      [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]*) shown=${tag:0:7} ;;
    esac
    note=""
    case "$image" in
      ghcr.io/muchq/*)
        expected=$(host_pin "$(sha_var_for "$svc")")
        [ -n "$expected" ] || expected=$(host_pin DEPLOY_SHA)
        if [ -n "$expected" ] && [ "$tag" != "$expected" ]; then
          note="  <- .env says ${expected:0:7}"
          drifted=$((drifted + 1))
        fi
        ;;
    esac
    # Anything not "Up ..." is down, restarting, or crash-looping; the status
    # string carries the exit code, and the restart count says how hard.
    case "$state" in
      Up*) ;;
      *) unhealthy=$((unhealthy + 1)) ;;
    esac
    printf '%-18s  %-9s  %-8s  %s%s\n' "$svc" "$shown" "${restarts[$svc]:-?}" "$state" "$note"
  done <<< "$(printf '%s\n' "$running" | sort)"

  if [ "$drifted" -gt 0 ] || [ "$unhealthy" -gt 0 ]; then
    echo
    [ "$drifted" -gt 0 ] && {
      echo "$drifted container(s) are not running the recorded revision — a deploy" >&2
      echo "may not have recreated them. A full deploy re-converges the stack." >&2
    }
    [ "$unhealthy" -gt 0 ] && {
      echo "$unhealthy container(s) are not up. A climbing restart count with a" >&2
      echo "low uptime is a crash loop; the exit code is in the state above." >&2
    }
    exit 1
  fi
  exit 0
fi

# Deploy targets are commits on main that CI has already published, so resolve
# against the remote rather than whatever the local checkout happens to be on.
git fetch --quiet origin main

if [ -n "$TARGET_SERVICE" ]; then
  deployed_sha=$(host_pin "$(sha_var_for "$TARGET_SERVICE")")
  # A service with no pin of its own is running the stack-wide revision.
  [ -n "$deployed_sha" ] || deployed_sha=$(host_pin DEPLOY_SHA)
else
  deployed_sha=$(host_pin DEPLOY_SHA)
fi

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
target_desc=${TARGET_SERVICE:-all services}

echo "Deploying $target_desc at $short_sha  $(describe "$DEPLOY_SHA")"
if [ -n "$deployed_sha" ] && [ "$deployed_sha" != "$DEPLOY_SHA" ]; then
  echo "Replacing $(git rev-parse --short "$deployed_sha" 2>/dev/null || echo "$deployed_sha")  $(describe "$deployed_sha")"
elif [ "$deployed_sha" = "$DEPLOY_SHA" ]; then
  echo "(already the deployed revision)"
fi

if [ "$ASSUME_YES" -ne 1 ]; then
  printf 'Proceed? [y/N] '
  read -r reply || reply=""
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
# host half-updated. Space-separated — this list is expanded into a remote `for`
# loop, where a newline would end the loop header instead of separating words.
if [ -n "$TARGET_SERVICE" ]; then
  images="ghcr.io/muchq/$TARGET_SERVICE"
else
  images=$(grep -o 'ghcr\.io/muchq/[a-z0-9_-]*' "$COMPOSE_FILE" | sort -u | tr '\n' ' ')
fi
echo "Verifying published images for $short_sha..."
missing=$(ssh "$HOST" "for img in $images; do sudo docker manifest inspect \$img:$DEPLOY_SHA >/dev/null 2>&1 || echo \$img; done")
if [ -n "$missing" ]; then
  echo "Error: no image published at $short_sha for:" >&2
  echo "$missing" | sed 's/^/  /' >&2
  echo "Either that commit's publish build is unfinished or failed, or the" >&2
  echo "service did not exist yet at that commit." >&2
  exit 1
fi

# A full deploy converges the stack on one revision, so it also clears the
# per-service pins; --service sets just its own.
all_pin_vars=$(pin_vars | tr '\n' ' ')
if [ -n "$TARGET_SERVICE" ]; then
  replaced_vars=$(sha_var_for "$TARGET_SERVICE")
  env_line="$replaced_vars=$DEPLOY_SHA"
  # Scoped to one service, so nothing here should reap the others.
  up_flags=""
else
  replaced_vars=$all_pin_vars
  env_line="DEPLOY_SHA=$DEPLOY_SHA"
  up_flags="--remove-orphans"
fi

# The scp below replaces the host's ~/.env wholesale, and the local copy holds
# secrets only — so carry over the pins this deploy isn't replacing. Without
# this a --service run drops DEPLOY_SHA, and every other service would come
# back on :latest at the next reboot.
preserved_pins=""
for var in $all_pin_vars; do
  case " $replaced_vars " in
    *" $var "*) continue ;;
  esac
  val=$(host_pin "$var")
  [ -n "$val" ] && preserved_pins="$preserved_pins $var=$val"
done

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

  # Record the revision being deployed. compose.yaml reads these to pin image
  # tags, so this file is what the stack comes back up on after an unattended
  # \`compose up\` (a reboot, a manual restart). Rewritten after the scp above,
  # which would otherwise have dropped the pins.
  touch ~/.env
  cp ~/.env ~/.env.tmp
  for var in ${all_pin_vars}; do
    grep -v "^\${var}=" ~/.env.tmp > ~/.env.next || true
    mv ~/.env.next ~/.env.tmp
  done
  for pin in ${preserved_pins}; do
    echo "\$pin" >> ~/.env.tmp
  done
  echo "${env_line}" >> ~/.env.tmp
  mv ~/.env.tmp ~/.env
  chmod 600 ~/.env
  # Export only after the rewrite: exporting the old file first would leave a
  # replaced pin in the environment, where it outranks the .env file.
  export \$(cat ~/.env | grep -v '^#' | xargs)

  sudo -E docker compose -f compose.yaml -f docker-compose.observability.yml pull ${TARGET_SERVICE}
  sudo -E docker compose -f compose.yaml -f docker-compose.observability.yml up -d ${up_flags} ${TARGET_SERVICE}

  # Reload Caddy configuration. Target the admin API on IPv4 explicitly: inside
  # the container \`localhost\` resolves to ::1 first, but Caddy's admin endpoint
  # listens only on 127.0.0.1:2019, so the default localhost reload is refused.
  sudo -E docker compose exec caddy caddy reload --config /etc/caddy/Caddyfile --address 127.0.0.1:2019
EOF

echo "Deployment complete! $target_desc running $short_sha"
