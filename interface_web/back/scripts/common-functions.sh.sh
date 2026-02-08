#!/bin/bash

# ==============================================
# THOTH CLOUD - Common Functions Library
# ==============================================
# Shared utilities for all setup scripts
# ==============================================

# Colors for output
export RED='\033[0;31m'
export GREEN='\033[0;32m'
export YELLOW='\033[1;33m'
export BLUE='\033[0;34m'
export NC='\033[0m' # No Color

# ==============================================
# LOGGING FUNCTIONS
# ==============================================

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_success() {
    echo -e "${GREEN}✓${NC} $1"
}

log_step() {
    echo -e "${BLUE}[STEP]${NC} $1"
}

# ==============================================
# VALIDATION FUNCTIONS
# ==============================================

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

check_command() {
    local cmd=$1
    if ! command -v "$cmd" &> /dev/null; then
        log_error "$cmd is not installed"
        return 1
    fi
    return 0
}

check_commands() {
    local missing=0
    for cmd in "$@"; do
        if ! check_command "$cmd"; then
            missing=1
        fi
    done
    
    if [ $missing -eq 1 ]; then
        log_error "Some required commands are missing"
        return 1
    fi
    
    log_success "All required commands are available"
    return 0
}

check_virtualization() {
    log_info "Checking CPU virtualization support..."
    if egrep -c '(vmx|svm)' /proc/cpuinfo > /dev/null; then
        log_success "CPU supports hardware virtualization"
        return 0
    else
        log_warn "CPU may not support hardware virtualization"
        return 1
    fi
}

# ==============================================
# SYSTEM FUNCTIONS
# ==============================================

update_system() {
    log_info "Updating system packages..."
    apt-get update -y >/dev/null 2>&1
    apt-get upgrade -y >/dev/null 2>&1
    log_success "System updated"
}

install_packages() {
    local packages=("$@")
    log_info "Installing packages: ${packages[*]}"
    
    if apt-get install -y "${packages[@]}" >/dev/null 2>&1; then
        log_success "Packages installed successfully"
        return 0
    else
        log_error "Failed to install packages"
        return 1
    fi
}

# ==============================================
# DOCKER FUNCTIONS
# ==============================================

check_docker() {
    if ! check_command docker; then
        log_error "Docker is not installed"
        return 1
    fi
    
    if ! check_command docker-compose; then
        log_error "docker-compose is not installed"
        return 1
    fi
    
    log_success "Docker and docker-compose are installed"
    return 0
}

install_docker() {
    log_info "Installing Docker..."
    
    # Install prerequisites
    apt-get install -y apt-transport-https ca-certificates curl software-properties-common
    
    # Add Docker GPG key
    curl -fsSL https://download.docker.com/linux/ubuntu/gpg | apt-key add -
    
    # Add Docker repository
    add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"
    
    # Install Docker
    apt-get update
    apt-get install -y docker-ce docker-ce-cli containerd.io
    
    # Install docker-compose
    curl -L "https://github.com/docker/compose/releases/latest/download/docker-compose-$(uname -s)-$(uname -m)" \
        -o /usr/local/bin/docker-compose
    chmod +x /usr/local/bin/docker-compose
    
    # Enable and start Docker
    systemctl enable docker
    systemctl start docker
    
    log_success "Docker installed successfully"
}

# ==============================================
# LIBVIRT FUNCTIONS
# ==============================================

install_libvirt() {
    log_info "Installing KVM, QEMU, and libvirt..."
    
    local packages=(
        qemu-kvm
        libvirt-daemon-system
        libvirt-daemon
        libvirt-clients
        bridge-utils
        virtinst
        virt-manager
        cpu-checker
        libguestfs-tools
        libosinfo-bin
        cloud-image-utils
        genisoimage
        qemu-utils
        whois        
    )
    
    install_packages "${packages[@]}"
    
    # Enable and start libvirtd
    systemctl enable libvirtd
    systemctl start libvirtd
    
    log_success "Libvirt installed and started"
}

configure_libvirt_remote() {
    log_info "Configuring libvirt for remote access..."
    
    # Backup original config if it exists
    if [ -f /etc/libvirt/libvirtd.conf ]; then
        cp /etc/libvirt/libvirtd.conf /etc/libvirt/libvirtd.conf.backup
    fi
    
    # Add remote access configuration
    cat >> /etc/libvirt/libvirtd.conf << 'EOF'

# THOTH CLOUD - Remote access configuration
unix_sock_group = "libvirt"
unix_sock_ro_perms = "0777"
unix_sock_rw_perms = "0770"
auth_unix_ro = "none"
auth_unix_rw = "none"
EOF
    
    # Restart libvirtd
    systemctl restart libvirtd
    
    log_success "Libvirt configured for remote access"

# Allow forwarding
sysctl -w net.ipv4.ip_forward=1
echo "net.ipv4.ip_forward=1" | tee -a /etc/sysctl.conf

# Configure firewall to allow socat connections
ufw allow 10000:60000/tcp comment "Port forwarding for VMs"

}

setup_default_network() {
    log_info "Setting up default libvirt network..."
    
    virsh net-autostart default || true
    virsh net-start default 2>/dev/null || log_warn "Default network already running"
    
    if virsh net-info default | grep -q "Active.*yes"; then
        log_success "Default network is active"
        return 0
    else
        log_warn "Default network may not be active"
        return 1
    fi
}

# ==============================================
# DIRECTORY FUNCTIONS
# ==============================================

create_directory() {
    local dir=$1
    local owner=${2:-root:root}
    local perms=${3:-755}
    
    if [ ! -d "$dir" ]; then
        mkdir -p "$dir"
        chown "$owner" "$dir"
        chmod "$perms" "$dir"
        log_success "Created directory: $dir"
    else
        log_info "Directory already exists: $dir"
    fi
}

create_directories() {
    local base_dir=$1
    shift
    local subdirs=("$@")
    
    create_directory "$base_dir"
    
    for subdir in "${subdirs[@]}"; do
        create_directory "$base_dir/$subdir"
    done
}

# ==============================================
# USER FUNCTIONS
# ==============================================

create_user_if_not_exists() {
    local username=$1
    local groups=${2:-""}
    
    if id "$username" &>/dev/null; then
        log_info "User '$username' already exists"
        return 0
    fi
    
    useradd -m -s /bin/bash "$username"
    log_success "User '$username' created"
    
    if [ -n "$groups" ]; then
        IFS=',' read -ra GROUP_ARRAY <<< "$groups"
        for group in "${GROUP_ARRAY[@]}"; do
            usermod -aG "$group" "$username"
        done
        log_success "User '$username' added to groups: $groups"
    fi
}


configure_passwordless_sudo() {
    local username=$1
    
    log_info "Configuring passwordless sudo for '$username'..."
    
    cat > "/etc/sudoers.d/$username" << EOF
# THOTH CLOUD - ${username} user passwordless sudo
${username} ALL=(ALL) NOPASSWD: ALL
EOF
    
    chmod 440 "/etc/sudoers.d/$username"
    log_success "Passwordless sudo configured for '$username'"
}

setup_ssh_for_user() {
    local username=$1
    local home_dir="/home/$username"
    
    log_info "Setting up SSH for user '$username'..."
    
    mkdir -p "$home_dir/.ssh"
    chmod 700 "$home_dir/.ssh"
    touch "$home_dir/.ssh/authorized_keys"
    chmod 600 "$home_dir/.ssh/authorized_keys"
    chown -R "$username:$username" "$home_dir/.ssh"
    
    log_success "SSH configured for '$username'"
}

# ==============================================
# FIREWALL FUNCTIONS
# ==============================================

configure_firewall() {
    if ! command -v ufw &> /dev/null; then
        log_warn "UFW not installed, skipping firewall configuration"
        return 0
    fi
    
    log_info "Configuring firewall rules..."
    
    # Allow SSH
    ufw allow ssh >/dev/null 2>&1 || true
    
    # Allow additional ports passed as arguments
    for port in "$@"; do
        ufw allow "$port" >/dev/null 2>&1 || true
        log_info "Allowed port: $port"
    done
    
    log_success "Firewall rules configured"
}

# ==============================================
# FILE OPERATIONS
# ==============================================

download_file() {
    local url=$1
    local dest=$2
    local description=${3:-"file"}
    
    if [ -f "$dest" ]; then
        log_info "$description already exists at $dest"
        return 0
    fi
    
    log_info "Downloading $description..."
    if wget -q --show-progress -O "$dest" "$url"; then
        log_success "$description downloaded"
        return 0
    else
        log_error "Failed to download $description"
        return 1
    fi
}

save_credentials() {
    local file=$1
    local content=$2
    
    echo "$content" > "$file"
    chmod 600 "$file"
    log_success "Credentials saved to $file"
}

# ==============================================
# SERVICE FUNCTIONS
# ==============================================

enable_and_start_service() {
    local service=$1
    
    log_info "Enabling and starting $service..."
    
    systemctl enable "$service" >/dev/null 2>&1
    systemctl start "$service" >/dev/null 2>&1
    
    if systemctl is-active --quiet "$service"; then
        log_success "$service is running"
        return 0
    else
        log_error "$service failed to start"
        return 1
    fi
}

restart_service() {
    local service=$1
    
    log_info "Restarting $service..."
    
    if systemctl restart "$service" >/dev/null 2>&1; then
        log_success "$service restarted"
        return 0
    else
        log_error "Failed to restart $service"
        return 1
    fi
}

# ==============================================
# VERIFICATION FUNCTIONS
# ==============================================

verify_service_active() {
    local service=$1
    
    if systemctl is-active --quiet "$service"; then
        log_success "✓ $service is running"
        return 0
    else
        log_error "✗ $service is not running"
        return 1
    fi
}

verify_file_exists() {
    local file=$1
    local description=${2:-"File"}
    
    if [ -f "$file" ]; then
        log_success "✓ $description exists"
        return 0
    else
        log_error "✗ $description does not exist"
        return 1
    fi
}

verify_directory_exists() {
    local dir=$1
    local description=${2:-"Directory"}
    
    if [ -d "$dir" ]; then
        log_success "✓ $description exists"
        return 0
    else
        log_error "✗ $description does not exist"
        return 1
    fi
}

# ==============================================
# SUMMARY FUNCTIONS
# ==============================================

print_header() {
    local title=$1
    local width=50
    
    echo ""
    echo "=========================================="
    printf "%*s\n" $(( (${#title} + width) / 2 )) "$title"
    echo "=========================================="
    echo ""
}

print_section() {
    local title=$1
    echo ""
    echo "---------- $title ----------"
    echo ""
}

print_info_line() {
    local label=$1
    local value=$2
    printf "  %-20s: %s\n" "$label" "$value"
}

# ==============================================
# SYSTEM INFO FUNCTIONS
# ==============================================

get_system_info() {
    echo "Hostname: $(hostname)"
    echo "IP Address: $(hostname -I | awk '{print $1}')"
    echo "OS: $(lsb_release -d | cut -f2)"
    echo "Kernel: $(uname -r)"
    echo "CPU Cores: $(nproc)"
    echo "Total RAM: $(free -h | awk '/^Mem:/ {print $2}')"
    echo "Disk Space: $(df -h / | awk 'NR==2 {print $4}') available"
}

# ==============================================
# CLEANUP FUNCTIONS
# ==============================================

cleanup_temp_files() {
    local pattern=${1:-"/tmp/thoth-*"}
    log_info "Cleaning up temporary files matching: $pattern"
    rm -rf $pattern 2>/dev/null || true
    log_success "Cleanup complete"
}

# ==============================================
# ERROR HANDLING
# ==============================================

handle_error() {
    local exit_code=$1
    local error_msg=$2
    
    log_error "$error_msg"
    log_error "Script failed with exit code: $exit_code"
    exit "$exit_code"
}

# Trap errors
trap 'handle_error $? "An error occurred on line $LINENO"' ERR

# ==============================================
# EXPORTS
# ==============================================

# Make all functions available to sourcing scripts
export -f log_info log_warn log_error log_success log_step
export -f check_root check_command check_commands check_virtualization
export -f update_system install_packages
export -f check_docker install_docker
export -f install_libvirt configure_libvirt_remote setup_default_network
export -f create_directory create_directories
export -f create_user_if_not_exists configure_passwordless_sudo setup_ssh_for_user
export -f configure_firewall
export -f download_file save_credentials
export -f enable_and_start_service restart_service
export -f verify_service_active verify_file_exists verify_directory_exists
export -f print_header print_section print_info_line
export -f get_system_info
export -f cleanup_temp_files handle_error

log_info "Common functions library loaded"