#!/bin/bash

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

if [[ $EUID -ne 0 ]]; then
   log_error "Must run as root (use sudo)"
   exit 1
fi

echo "Removing vps user and firewall rules..."

# Remove vps user
if id "vps" &>/dev/null; then
    userdel -r vps 2>/dev/null || userdel vps 2>/dev/null
    log_info "✓ User vps removed"
else
    log_info "User vps doesn't exist"
fi

# Remove sudo config
rm -f /etc/sudoers.d/vps
log_info "✓ Sudo config removed"

# Remove firewall rules
if command -v ufw &> /dev/null; then
    ufw delete allow 16509/tcp 2>/dev/null || true
    ufw delete allow 5900:5999/tcp 2>/dev/null || true
    log_info "✓ Firewall rules removed"
fi

log_info "Done!"