#!/bin/bash
# Run this on the remote host  X.X.X.X

# Create custom VM images directory
mkdir -p /home/thoth/vm-images
chmod 755 /home/thoth/vm-images

# Add user to libvirt groups
sudo usermod -a -G libvirt thoth
sudo usermod -a -G kvm thoth
sudo usermod -a -G libvirt-qemu thoth

# Restart libvirtd
sudo systemctl restart libvirtd

# Set proper permissions on default directory (if you have sudo)
sudo mkdir -p /var/lib/libvirt/images
sudo chown libvirt-qemu:kvm /var/lib/libvirt/images
sudo chmod 775 /var/lib/libvirt/images

echo "Setup complete! Log out and back in for group changes to take effect"


























#!/bin/bash

# THOTH CLOUD - Base Image Setup Script
# This script downloads and configures cloud images for VM deployment

set -e  # Exit on error

echo "=========================================="
echo "THOTH CLOUD - Base Image Setup"
echo "=========================================="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
BASE_IMAGE_DIR="/var/lib/libvirt/images/baseimage"
CLOUD_INIT_DIR="/var/lib/libvirt/images/cloud-init-iso"
IMAGES_DIR="/var/lib/libvirt/images"

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}❌ Please run as root (sudo)${NC}"
    exit 1
fi

echo -e "${GREEN}✅ Running as root${NC}"
echo ""

# Step 1: Create required directories
echo "📁 Creating required directories..."
mkdir -p "$BASE_IMAGE_DIR"
mkdir -p "$CLOUD_INIT_DIR"
mkdir -p "$IMAGES_DIR"

chown -R libvirt-qemu:kvm "$IMAGES_DIR"
chmod -R 755 "$IMAGES_DIR"

echo -e "${GREEN}✅ Directories created${NC}"
echo ""

# Step 2: Check if base image already exists
UBUNTU_IMAGE="$BASE_IMAGE_DIR/ubuntu-22.04-server-cloudimg-amd64.img"

if [ -f "$UBUNTU_IMAGE" ]; then
    echo -e "${YELLOW}⚠️  Ubuntu 22.04 cloud image already exists${NC}"
    echo "   Location: $UBUNTU_IMAGE"
    read -p "   Do you want to re-download? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "   Skipping download..."
        echo ""
    else
        rm -f "$UBUNTU_IMAGE"
    fi
fi

# Step 3: Download Ubuntu cloud image if needed
if [ ! -f "$UBUNTU_IMAGE" ]; then
    echo "📥 Downloading Ubuntu 22.04 Cloud Image..."
    echo "   This may take a few minutes..."
    
    DOWNLOAD_URL="https://cloud-images.ubuntu.com/jammy/current/jammy-server-cloudimg-amd64.img"
    
    wget -q --show-progress -O "$UBUNTU_IMAGE" "$DOWNLOAD_URL"
    
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✅ Download complete${NC}"
    else
        echo -e "${RED}❌ Download failed${NC}"
        exit 1
    fi
    
    # Set proper permissions
    chown libvirt-qemu:kvm "$UBUNTU_IMAGE"
    chmod 644 "$UBUNTU_IMAGE"
    
    echo ""
fi

# Step 4: Verify image integrity
echo "🔍 Verifying image integrity..."
if qemu-img info "$UBUNTU_IMAGE" > /dev/null 2>&1; then
    echo -e "${GREEN}✅ Image is valid${NC}"
    
    # Show image info
    echo ""
    echo "📊 Image Information:"
    qemu-img info "$UBUNTU_IMAGE" | grep -E "virtual size|disk size|file format"
else
    echo -e "${RED}❌ Image is corrupted${NC}"
    exit 1
fi

echo ""

# Step 5: Install required packages
echo "📦 Checking required packages..."

REQUIRED_PACKAGES=(
    "genisoimage"
    "qemu-utils"
    "libvirt-daemon"
    "libvirt-clients"
    "cloud-image-utils"
    "whois"  # For mkpasswd
)

MISSING_PACKAGES=()

for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if ! dpkg -l | grep -q "^ii  $pkg"; then
        MISSING_PACKAGES+=("$pkg")
    fi
done

if [ ${#MISSING_PACKAGES[@]} -ne 0 ]; then
    echo -e "${YELLOW}⚠️  Missing packages: ${MISSING_PACKAGES[*]}${NC}"
    echo "   Installing..."
    
    apt-get update -qq
    apt-get install -y -qq "${MISSING_PACKAGES[@]}"
    
    echo -e "${GREEN}✅ Packages installed${NC}"
else
    echo -e "${GREEN}✅ All required packages are installed${NC}"
fi

echo ""

# Step 6: Verify libvirt is running
echo "🔧 Checking libvirt status..."
if systemctl is-active --quiet libvirtd; then
    echo -e "${GREEN}✅ libvirtd is running${NC}"
else
    echo -e "${YELLOW}⚠️  libvirtd is not running, starting...${NC}"
    systemctl start libvirtd
    systemctl enable libvirtd
    echo -e "${GREEN}✅ libvirtd started${NC}"
fi

echo ""

# Step 7: Check default network
echo "🌐 Checking default network..."
if virsh net-info default > /dev/null 2>&1; then
    if virsh net-info default | grep -q "Active:.*yes"; then
        echo -e "${GREEN}✅ Default network is active${NC}"
    else
        echo -e "${YELLOW}⚠️  Default network exists but is inactive, starting...${NC}"
        virsh net-start default
        virsh net-autostart default
        echo -e "${GREEN}✅ Default network started${NC}"
    fi
else
    echo -e "${RED}❌ Default network does not exist${NC}"
    echo "   Creating default network..."
    
    # Create default network XML
    cat > /tmp/default-network.xml <<EOF
<network>
  <name>default</name>
  <forward mode='nat'/>
  <bridge name='virbr0' stp='on' delay='0'/>
  <ip address='192.168.122.1' netmask='255.255.255.0'>
    <dhcp>
      <range start='192.168.122.2' end='192.168.122.254'/>
    </dhcp>
  </ip>
</network>
EOF
    
    virsh net-define /tmp/default-network.xml
    virsh net-start default
    virsh net-autostart default
    rm /tmp/default-network.xml
    
    echo -e "${GREEN}✅ Default network created and started${NC}"
fi

echo ""

# Step 8: Summary
echo "=========================================="
echo "✅ SETUP COMPLETE!"
echo "=========================================="
echo ""
echo "📊 Summary:"
echo "   Base image: $UBUNTU_IMAGE"
echo "   Cloud-init ISO directory: $CLOUD_INIT_DIR"
echo "   Images directory: $IMAGES_DIR"
echo ""
echo "🚀 You can now deploy VMs using THOTH CLOUD!"
echo ""
echo "📝 Quick test deployment:"
echo "   curl -X POST http://localhost:3000/api/vms/deploy \\"
echo "     -H 'Content-Type: application/json' \\"
echo "     -d '{\"hostname\":\"test-vm\",\"memory\":1024,\"vcpus\":1,\"disk\":10,\"username\":\"ubuntu\",\"authMethod\":\"password\",\"password\":\"test123\"}'"
echo ""
echo "=========================================="