#!/bin/bash

# ==============================================
# THOTH CLOUD - Remote KVM Host Setup
# ==============================================
# Sets up a remote host for KVM virtualization
# with user access and remote management
# ==============================================

set -e

# Source common functions
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/common-functions.sh" || {
    echo "Error: Cannot load common-functions.sh"
    exit 1
}

# ==============================================
# CONFIGURATION
# ==============================================

readonly KVM_USER="vps"
readonly THOTH_DIR="/var/lib/thoth-cloud"
readonly IMAGE_DIR="/var/lib/libvirt/images"
readonly BASEIMG_DIR="${IMAGE_DIR}/baseimg"
readonly CLOUD_INIT_DIR="${IMAGE_DIR}/cloud-init-iso"
readonly LOG_DIR="/var/log/thoth-cloud"

# ==============================================
# MAIN SETUP FUNCTIONS
# ==============================================

setup_kvm_packages() {
    print_section "Installing KVM Packages"
    
    check_virtualization
    install_libvirt
    
    # Additional networking tools
    local net_packages=(
        net-tools
        iputils-ping
        dnsmasq
        iptables
    )
    install_packages "${net_packages[@]}"
}

setup_directories() {
    print_section "Creating Directory Structure"
    
    # Main directories
    create_directory "$THOTH_DIR" "root:root" "755"
    create_directory "$LOG_DIR" "root:root" "755"
    
    # Libvirt image directories
    create_directory "$IMAGE_DIR" "libvirt-qemu:kvm" "755"
    create_directory "$BASEIMG_DIR" "libvirt-qemu:kvm" "755"
    create_directory "$CLOUD_INIT_DIR" "libvirt-qemu:kvm" "755"
}

setup_kvm_user() {
    print_section "Setting Up KVM User"
    
    create_user_if_not_exists "$KVM_USER" "libvirt,kvm,sudo"
    configure_passwordless_sudo "$KVM_USER"
    setup_ssh_for_user "$KVM_USER"
}

configure_libvirt() {
    print_section "Configuring Libvirt"
    
    configure_libvirt_remote
    setup_default_network
}

create_config_files() {
    print_section "Creating Configuration Files"
    
    # Create .env file
    cat > "${THOTH_DIR}/.env" << 'EOF'
API_PORT=3000
HOSTS=qemu:///system
EOF
    chmod 644 "${THOTH_DIR}/.env"
    log_success ".env file created"
    
    # Create users.json
    cat > "${THOTH_DIR}/users.json" << 'EOF'
[]
EOF
    chmod 644 "${THOTH_DIR}/users.json"
    log_success "users.json created"
    
    # Create hosts.json
    cat > "${THOTH_DIR}/hosts.json" << 'EOF'
{
  "hosts": [
    {
      "id": "localhost",
      "uri": "qemu:///system",
      "hostname": "localhost"
    }
  ]
}
EOF
    chmod 644 "${THOTH_DIR}/hosts.json"
    log_success "hosts.json created"
}

setup_firewall_rules() {
    print_section "Configuring Firewall"
    
    configure_firewall "3000/tcp" "16509/tcp" "5900:5999/tcp"
}

create_test_script() {
    print_section "Creating Test Script"
    
    cat > "/home/${KVM_USER}/test_libvirt.sh" << 'EOF'
#!/bin/bash

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
    
    chmod +x "/home/${KVM_USER}/test_libvirt.sh"
    chown "${KVM_USER}:${KVM_USER}" "/home/${KVM_USER}/test_libvirt.sh"
    log_success "Test script created at /home/${KVM_USER}/test_libvirt.sh"
}

create_connection_info() {
    print_section "Creating Connection Information"
    
    local host_ip=$(hostname -I | awk '{print $1}')
    
    cat > "/home/${KVM_USER}/connection_info.txt" << EOF
THOTH CLOUD KVM Host Connection Information
============================================

Host IP: ${host_ip}
Username: ${KVM_USER}
Authentication: SSH key (passwordless sudo enabled)

Libvirt Connection URI:
  qemu+ssh://${KVM_USER}@${host_ip}/system

Backend Configuration (C++):
  Connection string: "qemu+ssh://${KVM_USER}@${host_ip}/system"
  
  If using custom SSH key:
  "qemu+ssh://${KVM_USER}@${host_ip}/system?keyfile=/path/to/private_key"

SSH Connection Test:
  ssh ${KVM_USER}@${host_ip}

Virsh Remote Test:
  virsh -c qemu+ssh://${KVM_USER}@${host_ip}/system list --all

Directory Structure:
  Config: ${THOTH_DIR}
  Images: ${IMAGE_DIR}
  Base Images: ${BASEIMG_DIR}
  Cloud-init ISOs: ${CLOUD_INIT_DIR}
  Logs: ${LOG_DIR}

Setup Date: $(date)
EOF
    
    chown "${KVM_USER}:${KVM_USER}" "/home/${KVM_USER}/connection_info.txt"
    log_success "Connection info saved to /home/${KVM_USER}/connection_info.txt"
}

verify_installation() {
    print_section "Verifying Installation"
    
    verify_service_active "libvirtd"
    
    if virsh net-info default | grep -q "Active.*yes"; then
        log_success "✓ Default network is active"
    else
        log_warn "✗ Default network is not active"
    fi
    
    if su - "$KVM_USER" -c "virsh list --all" &>/dev/null; then
        log_success "✓ ${KVM_USER} user can execute virsh commands"
    else
        log_error "✗ ${KVM_USER} user cannot execute virsh commands"
    fi
    
    if su - "$KVM_USER" -c "sudo -n true" &>/dev/null 2>&1; then
        log_success "✓ ${KVM_USER} user has passwordless sudo"
    else
        log_error "✗ ${KVM_USER} user does not have passwordless sudo"
    fi    
}

show_summary() {
    local host_ip=$(hostname -I | awk '{print $1}')
    
    print_header "Setup Complete!"
    
    echo "System Information:"
    get_system_info
    echo ""
    
    echo "KVM Configuration:"
    print_info_line "User" "$KVM_USER"
    print_info_line "Config Directory" "$THOTH_DIR"
    print_info_line "Images Directory" "$IMAGE_DIR"
    print_info_line "Base Images" "$BASEIMG_DIR"
    print_info_line "Connection URI" "qemu+ssh://${KVM_USER}@${host_ip}/system"
    echo ""
    
    echo "Next Steps:"
    echo "  1. Add your backend server's SSH public key to /home/${KVM_USER}/.ssh/authorized_keys"
    echo "  2. Test connection: ssh ${KVM_USER}@${host_ip}"
    echo "  3. Test libvirt: virsh -c qemu+ssh://${KVM_USER}@${host_ip}/system list"
    echo "  4. Run test script: su - ${KVM_USER} -c /home/${KVM_USER}/test_libvirt.sh"
    echo ""
    
    echo "SSH Key Setup:"
    echo "  On backend server:"
    echo "    ssh-keygen -t rsa -b 4096 -f ~/.ssh/thoth_kvm_key -N ''"
    echo "    ssh-copy-id -i ~/.ssh/thoth_kvm_key.pub ${KVM_USER}@${host_ip}"
    echo ""
    
    echo "Security Recommendations:"
    echo "  - Configure SSH key-only authentication (disable password auth)"
    echo "  - Set up fail2ban for SSH protection"
    echo "  - Review and harden firewall rules"
    echo ""
    
    echo "View connection details anytime:"
    echo "  cat /home/${KVM_USER}/connection_info.txt"
    echo ""
}

# ==============================================
# MAIN EXECUTION
# ==============================================

main() {
    print_header "THOTH CLOUD - Remote KVM Host Setup"
    
    # Check prerequisites
    check_root
    
    # Run setup steps
    update_system
    setup_kvm_packages
    setup_directories
    setup_kvm_user
    configure_libvirt
    create_config_files
    setup_firewall_rules
    create_test_script
    create_connection_info
    verify_installation
    show_summary
    
    log_success "Setup completed successfully! 🎉"
}

# Run main function
main "$@"