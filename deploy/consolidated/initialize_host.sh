#!/bin/bash
# Provisions the deploy host by running host_setup.sh on it. Safe to rerun.
set -euo pipefail

HOST=${HOST:-ubuntu@consolidated.cmptr.info}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Provisioning $HOST..."
# Copied and run as a file, with stdin closed: a package that prompts must
# fail its read rather than consume the script.
scp "$SCRIPT_DIR/host_setup.sh" "$HOST":~/host_setup.sh
ssh "$HOST" 'bash ~/host_setup.sh </dev/null'

echo ""
echo "If the kernel was upgraded, reboot to load it: ssh $HOST 'sudo reboot'"
