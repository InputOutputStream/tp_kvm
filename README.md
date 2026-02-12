# THOTH CLOUD - Complete Deployment Guide

## Table of Contents
- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Quick Start](#quick-start)
- [Detailed Setup](#detailed-setup)
- [Architecture](#architecture)
- [Troubleshooting](#troubleshooting)

## Overview

THOTH CLOUD is a multi-tenant cloud platform providing IaaS, PaaS, and SaaS services with:
- Virtual Machine management (KVM/libvirt)
- Docker Swarm orchestration
- PaaS application catalog (WordPress, Odoo, Moodle, etc.)
- SaaS collaboration suite (OwnCloud + OnlyOffice)

## Prerequisites

### System Requirements
- **OS**: Ubuntu 22.04 LTS or later
- **CPU**: 8+ cores with VT-x/AMD-V support
- **RAM**: 16GB minimum, 32GB recommended
- **Disk**: 100GB+ SSD
- **Network**: Static IP recommended

### Required Software
```bash
# Check virtualization support
egrep -c '(vmx|svm)' /proc/cpuinfo  # Should return > 0
```

## Quick Start

For a single-host development setup:

```bash
# 1. Clone repository
https://github.com/InputOutputStream/tp_kvm/
cd thoth-cloud

# 2. Run automated setup (Ubuntu 22.04+)
sudo bash scripts/set-remote.sh

# 3. Install development libraries
sudo bash scripts/setup-dev-libs.sh

# 4. Compile backend
mkdir build && cd build
cmake ..
make -j$(nproc)

# 5. Initialize databases
cd ..
sudo bash scripts/paas-db-setup.sh

# 6. Start backend
sudo ./build/thoth_cloud

# 7. Access frontend
# Open browser: http://localhost:3000
# Default credentials: admin / admin123
```

## Detailed Setup

### Step 1: Host Preparation

#### 1.1 Install Base System
```bash
# Update system
sudo apt update && sudo apt upgrade -y

# Install essential packages
sudo apt install -y git build-essential cmake pkg-config
```

#### 1.2 Configure KVM Host
```bash
cd thoth-cloud/interface_web/back/scripts

# Run comprehensive host setup
sudo bash set-remote.sh
```

**What this script does:**
- Installs KVM, QEMU, libvirt packages
- Creates `vps` user with passwordless sudo
- Configures libvirt for remote access
- Sets up directory structure:
  - `/var/lib/thoth-cloud` - Configuration files
  - `/var/lib/libvirt/images` - VM disks
  - `/var/lib/libvirt/images/baseimg` - Base images
  - `/var/lib/libvirt/images/cloud-init-iso` - Cloud-init ISOs
- Configures firewall (ports 3000, 16509, 5900-5999)
- Downloads Ubuntu 22.04 cloud image

**Verify installation:**
```bash
# Check libvirt
sudo systemctl status libvirtd
virsh list --all

# Check default network
virsh net-list --all
virsh net-info default

# Verify base image
ls -lh /var/lib/libvirt/images/baseimg/
```

#### 1.3 Set Permissions (if needed)
```bash
# Grant vps user access to thoth-cloud directory
sudo bash set_thoth_cloud_dir_perm.sh
```

### Step 2: Backend Compilation

#### 2.1 Install Dependencies
```bash
# Install C++ development libraries
sudo bash scripts/setup-dev-libs.sh
```

**Installed components:**
- libvirt-dev, libvirt-daemon-system
- cpp-httplib (REST API framework)
- nlohmann/json (JSON parsing)

Headers installed to: `interface_web/back/include/`

#### 2.2 Compile
```bash
cd interface_web/back

# Create build directory
mkdir -p build
cd build

# Configure and compile
cmake ..
make -j$(nproc)

# Binary created: ./thoth_cloud
```

**Build troubleshooting:**
```bash
# If cmake fails, install missing packages
sudo apt install -y libssl-dev uuid-dev

# Clean rebuild
rm -rf build && mkdir build && cd build
cmake .. && make clean && make -j$(nproc)
```

### Step 3: Database Setup

#### 3.1 Deploy Shared Databases
```bash
cd interface_web/back/scripts

# Install Docker if not present
sudo apt install -y docker.io docker-compose

# Deploy PostgreSQL + MariaDB containers
sudo bash paas-db-setup.sh
```

**What this creates:**
- `thoth-postgres` container (port 5432)
- `thoth-mariadb` container (port 3306)
- Network: `thoth_paas_network`
- Management script: `/var/lib/thoth-paas/manage-databases.sh`

**Verify databases:**
```bash
# Check containers
docker ps | grep thoth

# Test PostgreSQL
docker exec thoth-postgres psql -U postgres -c "\l"

# Test MariaDB
docker exec thoth-mariadb mysql -uroot -pthoth_mariadb_root_2024 -e "SHOW DATABASES;"
```

#### 3.2 Database Management
```bash
# Create database for user 'alice' app 'wordpress'
/var/lib/thoth-paas/manage-databases.sh create-mysql alice wordpress

# Create PostgreSQL database
/var/lib/thoth-paas/manage-databases.sh create-pg alice odoo

# List databases
/var/lib/thoth-paas/manage-databases.sh list-mysql
/var/lib/thoth-paas/manage-databases.sh list-pg

# Delete database
/var/lib/thoth-paas/manage-databases.sh delete-mysql alice_wordpress
```

### Step 4: Configuration

#### 4.1 Backend Configuration
```bash
# Create config directory
sudo mkdir -p /var/lib/thoth-cloud

# Create .env file
sudo tee /var/lib/thoth-cloud/.env <<EOF
API_PORT=3000
HOSTS=qemu:///system
EOF

# Create users.json (will be populated by backend)
echo "[]" | sudo tee /var/lib/thoth-cloud/users.json

# Create hosts.json
sudo tee /var/lib/thoth-cloud/hosts.json <<EOF
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
```

#### 4.2 Create Admin User
```bash
cd interface_web/back/build

# Create initial admin account
# Username: admin, Password: admin123
sudo ./thoth_cloud --create-admin
```

### Step 5: Start Backend

```bash
cd interface_web/back/build

# Run backend (foreground)
sudo ./thoth_cloud

# Or run in background
sudo nohup ./thoth_cloud > /var/log/thoth-cloud/backend.log 2>&1 &
```

**Backend should start on:** `http://localhost:3000`

**API endpoints:**
- `GET /api/health` - Health check
- `POST /api/auth/login` - User login
- `GET /api/vms/list` - List VMs
- `POST /api/vms/deploy` - Deploy VM

### Step 6: Frontend Deployment

#### 6.1 Static File Serving (Development)
```bash
cd interface_web/front

# Simple Python server
python3 -m http.server 8080

# Access: http://localhost:8080
```

#### 6.2 Nginx Production Setup
```bash
# Install Nginx
sudo apt install -y nginx

# Copy frontend files
sudo mkdir -p /var/www/thoth-cloud
sudo cp -r interface_web/front/* /var/www/thoth-cloud/

# Configure Nginx
sudo tee /etc/nginx/sites-available/thoth-cloud <<'EOF'
server {
    listen 80;
    server_name thoth-cloud.local;
    root /var/www/thoth-cloud;
    index index.html;

    # SPA routing
    location / {
        try_files $uri $uri/ /index.html;
    }

    # API proxy
    location /api {
        proxy_pass http://localhost:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_cache_bypass $http_upgrade;
    }

    # Security headers
    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;
}
EOF

# Enable site
sudo ln -s /etc/nginx/sites-available/thoth-cloud /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl reload nginx

# Add to /etc/hosts
echo "127.0.0.1 thoth-cloud.local" | sudo tee -a /etc/hosts
```

**Access:** `http://thoth-cloud.local`

### Step 7: Optional Components

#### 7.1 noVNC Console Access
```bash
cd interface_web/back/scripts

# Install noVNC for VM console access
sudo bash setup-novnc.sh
```

Enables VNC console access via browser at: `http://server-ip:6080`

#### 7.2 SaaS Suite (OwnCloud + OnlyOffice)
```bash
# Deploy collaborative workspace
sudo bash ownCloudSetup.sh

# Access:
# OwnCloud: http://localhost:8080
# OnlyOffice: http://localhost:8081
# Credentials in: /var/lib/thoth-saas/credentials.txt
```

## Multi-Host Setup

### Adding Remote KVM Hosts

#### On Remote Host:
```bash
# 1. Run setup script
sudo bash set-remote.sh

# 2. Note the IP address
hostname -I
```

#### On Backend Server:
```bash
# 1. Generate SSH key (if not exists)
ssh-keygen -t rsa -b 4096 -f ~/.ssh/thoth_kvm_key -N ''

# 2. Copy key to remote host
REMOTE_IP="192.168.1.100"
ssh-copy-id -i ~/.ssh/thoth_kvm_key.pub vps@$REMOTE_IP

# 3. Test connection
virsh -c qemu+ssh://vps@$REMOTE_IP/system list --all

# 4. Update configuration
sudo nano /var/lib/thoth-cloud/hosts.json
```

Add to `hosts.json`:
```json
{
  "hosts": [
    {
      "id": "localhost",
      "uri": "qemu:///system",
      "hostname": "localhost"
    },
    {
      "id": "kvm-node-1",
      "uri": "qemu+ssh://vps@192.168.1.100/system",
      "hostname": "kvm-node-1"
    }
  ]
}
```

Restart backend to apply changes.

## Usage Examples

### Deploy a VM
```bash
curl -X POST http://localhost:3000/api/vms/deploy \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "hostname": "web-server",
    "memory": 2048,
    "vcpus": 2,
    "disk": 20,
    "username": "ubuntu",
    "password": "secure123",
    "baseImage": "ubuntu-22.04-server-cloudimg-amd64"
  }'
```

### Deploy WordPress (PaaS)
```bash
curl -X POST http://localhost:3000/api/paas/deploy \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "appType": "wordpress",
    "appName": "myblog"
  }'
```

### Create Docker Swarm Cluster
```bash
curl -X POST http://localhost:3000/api/swarm/create \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{
    "clusterName": "production",
    "numManagers": 1,
    "numWorkers": 2
  }'
```

## Directory Structure Reference

```
/var/lib/thoth-cloud/          # Main config directory
├── .env                       # Environment variables
├── users.json                 # User database
├── hosts.json                 # KVM hosts configuration
├── swarm_clusters.json        # Swarm cluster state
└── port_forwards.json         # Port forwarding rules

/var/lib/thoth-paas/           # PaaS configuration
├── shared-databases/          # Docker compose for databases
├── manage-databases.sh        # DB management script
├── pg-credentials.txt         # PostgreSQL credentials
└── mysql-credentials.txt      # MariaDB credentials

/var/lib/thoth-saas/           # SaaS workspace
├── workspace/                 # Docker compose files
├── credentials.txt            # Access credentials
├── billing-tracker.sh         # Storage billing
└── provision-user.sh          # User provisioning

/var/lib/libvirt/images/       # VM storage
├── baseimg/                   # Base cloud images
├── cloud-init-iso/            # Cloud-init ISOs
└── *.qcow2                    # VM disk images

/var/log/thoth-cloud/          # Application logs
```

## Troubleshooting

### Backend won't start
```bash
# Check libvirt connection
virsh list

# Check port availability
sudo netstat -tlnp | grep 3000

# Check logs
sudo journalctl -u thoth-cloud -f

# Verify permissions
ls -la /var/lib/thoth-cloud/
```

### VMs fail to deploy
```bash
# Check base image exists
ls -lh /var/lib/libvirt/images/baseimg/

# Check network
virsh net-list --all
virsh net-start default

# Check disk space
df -h /var/lib/libvirt/images/

# Check cloud-init tools
which qemu-img genisoimage mkpasswd
```

### Database connection fails
```bash
# Check containers
docker ps | grep thoth

# Restart databases
docker restart thoth-postgres thoth-mariadb

# Check logs
docker logs thoth-postgres
docker logs thoth-mariadb
```

### Remote host connection fails
```bash
# Test SSH
ssh vps@remote-host

# Test libvirt
virsh -c qemu+ssh://vps@remote-host/system list

# Check firewall
sudo ufw status
```

## Maintenance

### Backup
```bash
# Backup configuration
sudo tar -czf thoth-backup-$(date +%F).tar.gz \
  /var/lib/thoth-cloud/ \
  /var/lib/thoth-paas/ \
  /var/lib/thoth-saas/

# Backup databases
docker exec thoth-postgres pg_dumpall -U postgres > postgres-backup.sql
docker exec thoth-mariadb mysqldump -uroot -pthoth_mariadb_root_2024 --all-databases > mariadb-backup.sql
```

### Updates
```bash
# Update base images
cd /var/lib/libvirt/images/baseimg
sudo wget -O ubuntu-22.04-server-cloudimg-amd64.img \
  https://cloud-images.ubuntu.com/jammy/current/jammy-server-cloudimg-amd64.img

# Rebuild backend
cd thoth-cloud/interface_web/back/build
git pull
make clean && make -j$(nproc)
sudo systemctl restart thoth-cloud
```

## Systemd Service (Production)

Create service file:
```bash
sudo tee /etc/systemd/system/thoth-cloud.service <<EOF
[Unit]
Description=THOTH Cloud Backend
After=network.target libvirtd.service docker.service

[Service]
Type=simple
User=root
WorkingDirectory=/opt/thoth-cloud/interface_web/back/build
ExecStart=/opt/thoth-cloud/interface_web/back/build/thoth_cloud
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF

# Enable and start
sudo systemctl daemon-reload
sudo systemctl enable thoth-cloud
sudo systemctl start thoth-cloud
sudo systemctl status thoth-cloud
```

## Security Recommendations

1. **Change default passwords** in all configuration files
2. **Use SSH keys** for remote host authentication
3. **Enable firewall** and restrict ports
4. **Use HTTPS** with SSL certificates (Let's Encrypt)
5. **Regular backups** of configuration and databases
6. **Update system** packages regularly
7. **Monitor logs** for suspicious activity
