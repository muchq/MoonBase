#!/bin/bash
# Provisions the deploy host. initialize_host.sh runs it over ssh; it runs as
# the login user and escalates with sudo.
#
# Every step converges rather than appends: a file is written only when its
# content differs, and a service is reloaded only when its file changed. Rerun
# it after editing it, or to bring a host back to this state. A rerun also
# applies pending upgrades, and an upgraded docker-ce or containerd.io
# restarts the daemon and every container with it.
set -euo pipefail

APT_OPTS=(-y -o Dpkg::Options::=--force-confdef -o Dpkg::Options::=--force-confold)

# Writes stdin to $1 with mode $2 when the content differs. Exit status 0
# means the file changed.
converge_file() {
  local path=$1 mode=$2 tmp
  tmp=$(mktemp)
  cat > "$tmp"
  if sudo test -f "$path" && sudo cmp -s "$tmp" "$path"; then
    rm -f "$tmp"
    return 1
  fi
  sudo install -m "$mode" -o root -g root "$tmp" "$path"
  rm -f "$tmp"
  echo "  wrote $path"
}

have_systemd() { [ -d /run/systemd/system ]; }

echo "Updating system packages..."
sudo env DEBIAN_FRONTEND=noninteractive apt-get update
sudo env DEBIAN_FRONTEND=noninteractive apt-get "${APT_OPTS[@]}" upgrade

echo "Removing distro Docker packages..."
for pkg in docker.io docker-doc docker-compose docker-compose-v2 podman-docker containerd runc; do
  if dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "install ok installed"; then
    sudo env DEBIAN_FRONTEND=noninteractive apt-get "${APT_OPTS[@]}" remove "$pkg"
  fi
done

echo "Setting up Docker's apt repository..."
sudo env DEBIAN_FRONTEND=noninteractive apt-get "${APT_OPTS[@]}" install ca-certificates curl
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc
codename=$(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}")
if echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/ubuntu $codename stable" \
  | converge_file /etc/apt/sources.list.d/docker.list 0644; then
  sudo env DEBIAN_FRONTEND=noninteractive apt-get update
fi

echo "Installing Docker..."
sudo env DEBIAN_FRONTEND=noninteractive apt-get "${APT_OPTS[@]}" install docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

# Before 28.0, Docker left loopback-bound published ports and unpublished
# container addresses reachable from hosts on the same link, which is what
# compose.yaml's port layout relies on not happening.
docker_version=$(dpkg-query -W -f='${Version}' docker-ce)
docker_major=${docker_version#*:}
docker_major=${docker_major%%.*}
if [ "$docker_major" -lt 28 ]; then
  echo "docker-ce $docker_version is older than 28.0; refusing to continue" >&2
  exit 1
fi

echo "Enabling unattended security upgrades..."
sudo env DEBIAN_FRONTEND=noninteractive apt-get "${APT_OPTS[@]}" install unattended-upgrades
converge_file /etc/apt/apt.conf.d/20auto-upgrades 0644 <<'EOF' || true
APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
EOF

echo "Hardening sshd..."
# Disabling password login on a host whose login user has no key would lock
# everyone out.
if ! grep -Eq '(ssh|ecdsa|sk)-[a-z0-9@.-]+ AAAA' ~/.ssh/authorized_keys 2>/dev/null; then
  echo "~/.ssh/authorized_keys holds no key; refusing to disable password login" >&2
  exit 1
fi
# sshd takes the first value it reads for each option, and Ubuntu's own
# drop-ins are numbered 50 and up, so this one wins.
if converge_file /etc/ssh/sshd_config.d/10-moonbase.conf 0644 <<'EOF'; then
PasswordAuthentication no
KbdInteractiveAuthentication no
PermitRootLogin no
EOF
  # The privilege-separation directory sshd -t insists on is created by the
  # service, which socket activation may not have started yet.
  sudo install -d -m 0755 /run/sshd
  if ! sudo sshd -t; then
    sudo rm -f /etc/ssh/sshd_config.d/10-moonbase.conf
    echo "sshd rejected the hardened config; removed it" >&2
    exit 1
  fi
  if have_systemd; then
    sudo systemctl try-reload-or-restart ssh
  fi
fi

echo "Installing fail2ban for sshd..."
# The systemd backend reads the journal, so the jail works whether or not
# the host also writes /var/log/auth.log. It covers the host's sshd only:
# Forgejo's SSH on 222 is a published Docker port, whose traffic never meets
# the INPUT chain fail2ban bans in.
sudo env DEBIAN_FRONTEND=noninteractive apt-get "${APT_OPTS[@]}" install fail2ban python3-systemd
if converge_file /etc/fail2ban/jail.d/10-moonbase.local 0644 <<'EOF'; then
[sshd]
enabled = true
backend = systemd
EOF
  if have_systemd; then
    sudo systemctl enable fail2ban
    sudo systemctl restart fail2ban
  fi
fi

echo "Creating the Forgejo config directory..."
sudo install -d -o 1000 -g 1000 /etc/forgejo

echo "Host setup complete."
