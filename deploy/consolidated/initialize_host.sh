#!/bin/bash
# Provisions the deploy host by running host_setup.sh on it. Safe to rerun.
set -euo pipefail

HOST=${HOST:-ubuntu@consolidated.cmptr.info}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Provisioning $HOST..."
ssh "$HOST" 'bash -s' < "$SCRIPT_DIR/host_setup.sh"

echo ""
echo "If the kernel was upgraded, reboot to load it: ssh $HOST 'sudo reboot'"
