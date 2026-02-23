#!/bin/bash
set -e

# Check if db_config file is provided
if [ -z "$1" ]; then
  echo "Error: db_config file path required"
  echo "Usage: $0 <path_to_db_config>"
  exit 1
fi

DB_CONFIG_FILE="$1"

if [ ! -f "$DB_CONFIG_FILE" ]; then
  echo "Error: db_config file not found at $DB_CONFIG_FILE"
  exit 1
fi

echo "Initializing fresh Lightsail instance for api.muchq.com..."

ssh ubuntu@api.muchq.com << 'EOF'
  # Update and upgrade system packages
  echo "Updating system packages..."
  sudo apt update && sudo apt upgrade -y

  # Remove old Docker packages if they exist
  echo "Removing old Docker packages..."
  for pkg in docker.io docker-doc docker-compose docker-compose-v2 podman-docker containerd runc; do
    sudo apt-get remove $pkg -y 2>/dev/null || true
  done

  # Add Docker's official GPG key
  echo "Setting up Docker repository..."
  sudo apt-get update
  sudo apt-get install ca-certificates curl -y
  sudo install -m 0755 -d /etc/apt/keyrings
  sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
  sudo chmod a+r /etc/apt/keyrings/docker.asc

  # Add the repository to Apt sources
  echo \
    "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu \
    $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}") stable" | \
    sudo tee /etc/apt/sources.list.d/docker.list > /dev/null
  sudo apt-get update

  # Install Docker
  echo "Installing Docker..."
  sudo apt-get install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin -y

  # Create r3dr config directory
  echo "Creating r3dr config directory..."
  sudo mkdir -p /etc/r3dr

  # Create Forgejo config directory
  echo "Creating Forgejo config directory..."
  sudo mkdir -p /etc/forgejo
  sudo chown 1000:1000 /etc/forgejo

  echo "Docker installation complete!"
EOF

# Copy db_config file
echo "Copying db_config file..."
scp "$DB_CONFIG_FILE" ubuntu@api.muchq.com:~/db_config
ssh ubuntu@api.muchq.com "sudo mv ~/db_config /etc/r3dr/db_config"

echo ""
echo "Host initialization complete!"
echo "Note: You may need to reboot the instance for all changes to take effect."
echo "Run: ssh ubuntu@api.muchq.com 'sudo reboot'"
