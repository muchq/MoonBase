#!/bin/bash
set -e

if [ -z "$1" ]; then
  echo "Usage: $0 <hostname>"
  echo "Example: $0 api.muchq.com"
  exit 1
fi

HOSTNAME=$1

echo "Running diagnostics on $HOSTNAME..."
echo "========================================"
echo ""

ssh ubuntu@$HOSTNAME << 'EOF'
  echo "=== Current Container Resource Usage ==="
  echo "Shows real-time CPU%, Memory usage/limit for each container"
  echo ""
  sudo docker stats --no-stream --format "table {{.Name}}\t{{.CPUPerc}}\t{{.MemUsage}}\t{{.MemPerc}}"
  echo ""
  echo ""

  echo "=== Container CPU Throttling Stats ==="
  echo "Shows how many times each container hit CPU limits"
  echo ""
  for container in $(sudo docker ps --format '{{.Names}}'); do
    echo "Container: $container"
    THROTTLED=$(sudo docker stats $container --no-stream --format "{{.Name}}" 2>/dev/null)
    if [ -n "$THROTTLED" ]; then
      # Get cgroup path for the container
      CONTAINER_ID=$(sudo docker inspect -f '{{.Id}}' $container)
      CGROUP_PATH="/sys/fs/cgroup/system.slice/docker-${CONTAINER_ID}.scope"

      if [ -f "${CGROUP_PATH}/cpu.stat" ]; then
        echo "  Throttle stats:"
        grep "throttled" ${CGROUP_PATH}/cpu.stat | while read line; do
          echo "    $line"
        done
      else
        echo "  (cgroup v2 path not found, trying alternate paths)"
        # Try docker's cgroup path
        ALT_PATH="/sys/fs/cgroup/docker/${CONTAINER_ID}"
        if [ -d "$ALT_PATH" ]; then
          if [ -f "${ALT_PATH}/cpu.stat" ]; then
            grep "throttled" ${ALT_PATH}/cpu.stat | while read line; do
              echo "    $line"
            done
          fi
        fi
      fi
    fi
    echo ""
  done
  echo ""

  echo "=== Container Memory Stats ==="
  echo "Shows memory usage details for each container"
  echo ""
  for container in $(sudo docker ps --format '{{.Names}}'); do
    echo "Container: $container"
    sudo docker stats $container --no-stream --format "  Memory: {{.MemUsage}} ({{.MemPerc}})"
    echo ""
  done
  echo ""

  echo "=== Recent Container Events (last 50) ==="
  echo "Shows OOM kills, restarts, and other container events"
  echo ""
  sudo docker events --since 24h --until 1s --filter 'event=oom' --filter 'event=kill' --filter 'event=die' --filter 'event=restart' 2>/dev/null | tail -50 || echo "No recent OOM/kill/restart events in last 24h"
  echo ""
  echo ""

  echo "=== Container Logs - Last 20 Lines (Error/Warning) ==="
  echo "Checking for OOM or resource-related errors"
  echo ""
  for container in $(sudo docker ps --format '{{.Names}}'); do
    echo "Container: $container"
    sudo docker logs $container --tail 20 2>&1 | grep -iE "oom|memory|killed|error|warn|throttle" || echo "  No obvious errors found"
    echo ""
  done
  echo ""

  echo "=== System Resource Usage ==="
  echo "Overall host system resources"
  echo ""
  echo "CPU Usage:"
  top -bn1 | grep "Cpu(s)" | sed "s/.*, *\([0-9.]*\)%* id.*/\1/" | awk '{print "  Idle: " $1 "%"}'
  echo ""
  echo "Memory Usage:"
  free -h
  echo ""
  echo "Disk Usage:"
  df -h / | tail -1
  echo ""
  echo ""

  echo "=== Docker System Info ==="
  sudo docker system df
  echo ""
EOF

echo ""
echo "========================================"
echo "Diagnostics complete!"
echo ""
echo "Key things to look for:"
echo "  - CPUPerc near or at the limit (e.g., 25% for 0.25 CPU limit)"
echo "  - nr_throttled > 0 indicates CPU throttling occurred"
echo "  - MemPerc at 100% or OOM events indicate memory limits hit"
echo "  - Container restarts may indicate resource issues"