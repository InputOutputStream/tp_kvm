#!/bin/bash

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

if [[ $EUID -ne 0 ]]; then
   log_error "Must run as root (use sudo)"
   exit 1
fi

log_info "Starting THOTH CLOUD Remote KVM Host Setup..."
echo "========================================"

# Update system
log_info "Updating system packages..."
apt-get update -y
apt-get upgrade -y

# Check virtualization
log_info "Checking CPU virtualization..."
if egrep -c '(vmx|svm)' /proc/cpuinfo > /dev/null; then
    log_info "✓ CPU supports virtualization"
else
    log_warn "CPU may not support virtualization"
fi

# Install KVM packages
log_info "Installing KVM, QEMU, and libvirt..."
apt-get install -y \
    qemu-kvm libvirt-daemon-system libvirt-daemon libvirt-clients \
    bridge-utils virtinst virt-manager cpu-checker libguestfs-tools \
    libosinfo-bin cloud-image-utils genisoimage qemu-utils \
    net-tools iputils-ping dnsmasq iptables whois

# Enable libvirtd
systemctl enable libvirtd
systemctl start libvirtd

# Create required directories
log_info "Creating THOTH CLOUD directories..."
mkdir -p /var/lib/thoth-cloud
mkdir -p /var/lib/libvirt/images/baseimg
mkdir -p /var/lib/libvirt/images/cloud-init-iso
mkdir -p /var/log/thoth-cloud

# Set ownership and permissions
chown -R libvirt-qemu:kvm /var/lib/libvirt/images
chmod -R 755 /var/lib/libvirt/images
chmod 755 /var/lib/thoth-cloud
chmod 755 /var/log/thoth-cloud

# Configure libvirt for group access
log_info "Configuring libvirt permissions..."
cat >> /etc/libvirt/libvirtd.conf << 'EOF'

# THOTH CLOUD - Group access configuration
unix_sock_group = "libvirt"
unix_sock_ro_perms = "0777"
unix_sock_rw_perms = "0770"
auth_unix_ro = "none"
auth_unix_rw = "none"
EOF

systemctl restart libvirtd

# Setup default network
log_info "Configuring default network..."
virsh net-autostart default || true
virsh net-start default 2>/dev/null || log_warn "Default network already running"

# Download Ubuntu cloud image
log_info "Downloading Ubuntu 22.04 cloud image..."
CLOUD_IMAGE="/var/lib/libvirt/images/baseimg/ubuntu-22.04-server-cloudimg-amd64.img"
if [ ! -f "$CLOUD_IMAGE" ]; then
    wget -q --show-progress -O "$CLOUD_IMAGE" \
        https://cloud-images.ubuntu.com/jammy/current/jammy-server-cloudimg-amd64.img
    chown libvirt-qemu:kvm "$CLOUD_IMAGE"
    chmod 644 "$CLOUD_IMAGE"
    log_info "✓ Cloud image downloaded"
else
    log_warn "Cloud image already exists"
fi

# Configure firewall
if command -v ufw &> /dev/null; then
    log_info "Configuring firewall..."
    ufw allow ssh
    ufw allow 3000/tcp     # API
    ufw allow 16509/tcp    # libvirt TLS
    ufw allow 5900:5999/tcp # VNC
fi

# Verify permissions
log_info "Verifying permissions..."
ls -la /var/lib/thoth-cloud/
ls -la /var/lib/libvirt/images/
ls -la /var/lib/libvirt/images/baseimg/

# Final checks
log_info "Running verification..."
systemctl is-active --quiet libvirtd && log_info "✓ libvirtd running" || log_error "✗ libvirtd not running"
virsh net-info default | grep -q "Active.*yes" && log_info "✓ Network active" || log_warn "✗ Network not active"
[ -f "$CLOUD_IMAGE" ] && log_info "✓ Base image exists" || log_error "✗ Base image missing"

# Display info
echo ""
log_info "============================================"
log_info "Setup Complete!"
log_info "============================================"
echo ""
echo "Directory Structure:"
echo "  /var/lib/thoth-cloud/          - Config files (.env, users.json, hosts.json)"
echo "  /var/lib/libvirt/images/       - VM disks (owned by libvirt-qemu:kvm)"
echo "  /var/lib/libvirt/images/baseimg/ - Base cloud images"
echo "  /var/lib/libvirt/images/cloud-init-iso/ - Cloud-init ISOs"
echo "  /var/log/thoth-cloud/          - Application logs"
echo ""
echo "Permissions:"
echo "  /var/lib/libvirt/images/       - 755 libvirt-qemu:kvm"
echo "  /var/lib/thoth-cloud/          - 755 root:root"
echo "  Config files                   - 644 root:root"
echo ""
echo "Connection URI:"
echo "  qemu:///system"
echo ""
echo "Next: Deploy your THOTH CLOUD backend to this host"
echo ""