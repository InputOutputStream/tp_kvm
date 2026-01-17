#!/bin/bash


set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running as root
if [[ $EUID -ne 0 ]]; then
   log_error "This script must be run as root (use sudo)"
   exit 1
fi

log_info "Starting THOTH CLOUD KVM Host Setup..."
echo "========================================"

# 1. Update system
log_info "Updating system packages..."
apt-get update -y
apt-get upgrade -y

# 2. Check if virtualization is supported
log_info "Checking CPU virtualization support..."
if egrep -c '(vmx|svm)' /proc/cpuinfo > /dev/null; then
    log_info "✓ CPU supports hardware virtualization"
else
    log_error "CPU does not support hardware virtualization (vmx/svm)"
    log_warn "VMs may not work properly or may be very slow"
fi

# 3. Install KVM and libvirt packages
log_info "Installing KVM, QEMU, and libvirt..."
apt-get install -y \
    qemu-kvm \
    libvirt-daemon-system \
    libvirt-daemon \
    libvirt-clients \
    bridge-utils \
    virtinst \
    virt-manager \
    cpu-checker \
    libguestfs-tools \
    libosinfo-bin \
    cloud-image-utils \
    genisoimage \
    qemu-utils

# 4. Verify KVM installation
log_info "Verifying KVM installation..."
if kvm-ok > /dev/null 2>&1; then
    log_info "✓ KVM is properly installed and working"
else
    log_warn "kvm-ok check failed, but continuing..."
fi

# 5. Enable and start libvirtd service
log_info "Enabling and starting libvirtd service..."
systemctl enable libvirtd
systemctl start libvirtd
systemctl status libvirtd --no-pager | head -n 5

# 6. Create vps user
log_info "Creating 'vps' user..."
if id "vps" &>/dev/null; then
    log_warn "User 'vps' already exists, skipping creation"
else
    useradd -m -s /bin/bash vps
    log_info "✓ User 'vps' created"
fi

# 7. Add vps user to necessary groups
log_info "Adding 'vps' user to libvirt, kvm, and sudo groups..."
usermod -aG libvirt vps
usermod -aG kvm vps
usermod -aG sudo vps

# 8. Configure passwordless sudo for vps user
log_info "Configuring passwordless sudo for 'vps' user..."
cat > /etc/sudoers.d/vps << EOF
# THOTH CLOUD - vps user passwordless sudo
vps ALL=(ALL) NOPASSWD: ALL
EOF

chmod 440 /etc/sudoers.d/vps
log_info "✓ Passwordless sudo configured"

# 9. Configure libvirt for remote access via SSH
log_info "Configuring libvirt for remote access..."

# Allow libvirt unix socket access for group
cat >> /etc/libvirt/libvirtd.conf << 'EOF'

# THOTH CLOUD - Remote access configuration
unix_sock_group = "libvirt"
unix_sock_ro_perms = "0777"
unix_sock_rw_perms = "0770"
auth_unix_ro = "none"
auth_unix_rw = "none"
EOF

# Restart libvirtd to apply changes
systemctl restart libvirtd

# 10. Setup SSH for vps user (for remote access)
log_info "Setting up SSH access for 'vps' user..."
mkdir -p /home/vps/.ssh
chmod 700 /home/vps/.ssh
touch /home/vps/.ssh/authorized_keys
chmod 600 /home/vps/.ssh/authorized_keys
chown -R vps:vps /home/vps/.ssh

# 11. Configure default libvirt network
log_info "Setting up default libvirt network..."
virsh net-autostart default || true
virsh net-start default || log_warn "Default network already started"

# 12. Create storage pool directory
log_info "Creating storage pool directory..."
mkdir -p /var/lib/libvirt/images
chmod 755 /var/lib/libvirt/images

# Verify storage pool
virsh pool-info default || log_warn "Default storage pool not found"

# 13. Download Ubuntu cloud image (for cloud-init VMs)
log_info "Downloading Ubuntu 22.04 cloud image..."
IMAGES_DIR="/var/lib/libvirt/images"
CLOUD_IMAGE="$IMAGES_DIR/ubuntu-22.04-server-cloudimg-amd64.img"

if [ ! -f "$CLOUD_IMAGE" ]; then
    wget -O "$CLOUD_IMAGE" \
        https://cloud-images.ubuntu.com/jammy/current/jammy-server-cloudimg-amd64.img
    log_info "✓ Ubuntu cloud image downloaded"
else
    log_warn "Ubuntu cloud image already exists"
fi

# 14. Install additional networking tools
log_info "Installing networking tools..."
apt-get install -y \
    net-tools \
    iputils-ping \
    dnsmasq \
    iptables

# 15. Configure firewall (if ufw is installed)
if command -v ufw &> /dev/null; then
    log_info "Configuring firewall rules..."
    ufw allow ssh
    ufw allow 16509/tcp  # libvirt TLS
    ufw allow 5900:5999/tcp  # VNC consoles
    log_info "✓ Firewall rules configured"
fi

# 16. Test libvirt connectivity as vps user
log_info "Testing libvirt connectivity..."
su - vps -c "virsh list --all" > /dev/null 2>&1 && \
    log_info "✓ vps user can connect to libvirt" || \
    log_error "vps user cannot connect to libvirt"

# 17. Display SSH public key info for remote access
log_info "============================================"
log_info "Setup Complete!"
log_info "============================================"
echo ""
log_info "To enable remote access from your backend server:"
echo ""
echo "1. On your BACKEND server, generate SSH key (if not exists):"
echo "   ssh-keygen -t rsa -b 4096 -f ~/.ssh/thoth_kvm_key -N ''"
echo ""
echo "2. Copy the public key to this KVM host:"
echo "   ssh-copy-id -i ~/.ssh/thoth_kvm_key.pub vps@$(hostname -I | awk '{print $1}')"
echo ""
echo "3. Test connection from backend:"
echo "   ssh -i ~/.ssh/thoth_kvm_key vps@$(hostname -I | awk '{print $1}')"
echo ""
echo "4. Test libvirt connection from backend:"
echo "   virsh -c qemu+ssh://vps@$(hostname -I | awk '{print $1}')/system list --all"
echo ""
log_info "Or add the backend server's public key manually:"
echo "   cat >> /home/vps/.ssh/authorized_keys << 'EOF'"
echo "   <paste your backend server's public key here>"
echo "   EOF"
echo ""

# 18. Display connection URI for backend
log_info "============================================"
log_info "Libvirt Connection URIs:"
log_info "============================================"
echo ""
echo "For local connection:"
echo "  qemu:///system"
echo ""
echo "For remote SSH connection (from backend):"
echo "  qemu+ssh://vps@$(hostname -I | awk '{print $1}')/system"
echo ""
echo "For remote SSH with custom key:"
echo "  qemu+ssh://vps@$(hostname -I | awk '{print $1}')/system?keyfile=/path/to/key"
echo ""

# 19. Display user information
log_info "============================================"
log_info "User Information:"
log_info "============================================"
echo ""
echo "Username: vps"
echo "Groups: $(groups vps)"
echo "Home: /home/vps"
echo "Sudo: PASSWORDLESS (no password required)"
echo "SSH: Configured in /home/vps/.ssh/"
echo ""

# 20. Display system information
log_info "============================================"
log_info "System Information:"
log_info "============================================"
echo ""
echo "Hostname: $(hostname)"
echo "IP Address: $(hostname -I | awk '{print $1}')"
echo "OS: $(lsb_release -d | cut -f2)"
echo "Kernel: $(uname -r)"
echo "CPU Cores: $(nproc)"
echo "Total RAM: $(free -h | awk '/^Mem:/ {print $2}')"
echo "Disk Space: $(df -h / | awk 'NR==2 {print $4}')"
echo ""

# 21. Show libvirt version
log_info "Libvirt version: $(virsh version --daemon | grep -i 'running hypervisor' | awk '{print $3, $4}')"
echo ""

# 22. Create a test script for backend
log_info "Creating test script for backend connection..."
cat > /home/vps/test_libvirt.sh << 'EOF'
#!/bin/bash
# Test script to verify libvirt functionality

echo "Testing libvirt connection..."
virsh list --all
echo ""

echo "Libvirt version:"
virsh version
echo ""

echo "Available storage pools:"
virsh pool-list --all
echo ""

echo "Available networks:"
virsh net-list --all
echo ""

echo "System capabilities:"
virsh capabilities | grep -E '<arch>|<machine>|<domain type' | head -n 10
echo ""

echo "If you see output above, libvirt is working correctly!"
EOF

chmod +x /home/vps/test_libvirt.sh
chown vps:vps /home/vps/test_libvirt.sh
log_info "✓ Test script created at /home/vps/test_libvirt.sh"

# 23. Final verification
log_info "============================================"
log_info "Running final verification..."
log_info "============================================"
echo ""

# Check if libvirtd is running
if systemctl is-active --quiet libvirtd; then
    log_info "✓ libvirtd service is running"
else
    log_error "✗ libvirtd service is not running"
fi

# Check if default network is active
if virsh net-info default | grep -q "Active.*yes"; then
    log_info "✓ Default network is active"
else
    log_warn "✗ Default network is not active"
fi

# Check vps user permissions
if su - vps -c "virsh list --all" &>/dev/null; then
    log_info "✓ vps user can execute virsh commands"
else
    log_error "✗ vps user cannot execute virsh commands"
fi

# Check sudo permissions
if su - vps -c "sudo -n true" &>/dev/null 2>&1; then
    log_info "✓ vps user has passwordless sudo"
else
    log_error "✗ vps user does not have passwordless sudo"
fi

echo ""
log_info "============================================"
log_info "Setup completed successfully! 🎉"
log_info "============================================"
echo ""
log_info "Next steps:"
echo "  1. Add your backend server's SSH public key to /home/vps/.ssh/authorized_keys"
echo "  2. Test connection: ssh vps@$(hostname -I | awk '{print $1}')"
echo "  3. Test libvirt: virsh -c qemu+ssh://vps@$(hostname -I | awk '{print $1}')/system list"
echo "  4. Run test script: su - vps -c /home/vps/test_libvirt.sh"
echo ""
log_info "For security, consider:"
echo "  - Configuring SSH key-only authentication (disable password auth)"
echo "  - Setting up firewall rules with ufw or iptables"
echo "  - Enabling fail2ban for SSH protection"
echo ""

# 24. Create connection info file
cat > /home/vps/connection_info.txt << EOF
THOTH CLOUD KVM Host Connection Information
============================================

Host IP: $(hostname -I | awk '{print $1}')
Username: vps
Authentication: SSH key (passwordless sudo enabled)

Libvirt Connection URI:
  qemu+ssh://vps@$(hostname -I | awk '{print $1}')/system

Backend Configuration (C++):
  Connection string: "qemu+ssh://vps@$(hostname -I | awk '{print $1}')/system"
  
  If using custom SSH key:
  "qemu+ssh://vps@$(hostname -I | awk '{print $1}')/system?keyfile=/path/to/private_key"

SSH Connection Test:
  ssh vps@$(hostname -I | awk '{print $1}')

Virsh Remote Test:
  virsh -c qemu+ssh://vps@$(hostname -I | awk '{print $1}')/system list --all

Storage Location:
  /var/lib/libvirt/images/

Cloud Image:
  /var/lib/libvirt/images/ubuntu-22.04-server-cloudimg-amd64.img

Setup Date: $(date)
EOF

chown vps:vps /home/vps/connection_info.txt
log_info "✓ Connection info saved to /home/vps/connection_info.txt"

echo ""
log_info "You can view connection details anytime:"
echo "  cat /home/vps/connection_info.txt"
echo ""