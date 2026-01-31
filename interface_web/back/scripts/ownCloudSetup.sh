#!/bin/bash

# ==============================================
# THOTH CLOUD - OwnCloud + OnlyOffice SaaS Setup
# ==============================================
# Complete Google Workspace alternative
# - OwnCloud for file storage/sharing
# - OnlyOffice for document editing
# - Integrated with billing system
# ==============================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SAAS_DIR="/var/lib/thoth-saas"
COMPOSE_DIR="$SAAS_DIR/workspace"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Configuration
OWNCLOUD_DOMAIN="${OWNCLOUD_DOMAIN:-cloud.thoth.local}"
ONLYOFFICE_DOMAIN="${ONLYOFFICE_DOMAIN:-office.thoth.local}"
ADMIN_USER="${ADMIN_USER:-admin}"
ADMIN_PASS="${ADMIN_PASS:-Admin@2024}"
MYSQL_ROOT_PASS="owncloud_mysql_$(openssl rand -hex 8)"
MYSQL_OWNCLOUD_PASS="owncloud_$(openssl rand -hex 8)"

check_requirements() {
    log_info "Checking requirements..."
    
    for cmd in docker docker-compose; do
        if ! command -v $cmd &> /dev/null; then
            log_error "$cmd is not installed"
            exit 1
        fi
    done
    
    log_info "✓ Requirements met"
}

setup_directories() {
    log_info "Setting up directories..."
    
    mkdir -p "$SAAS_DIR"
    mkdir -p "$COMPOSE_DIR"
    mkdir -p "$SAAS_DIR/owncloud"
    mkdir -p "$SAAS_DIR/onlyoffice"
    mkdir -p "$SAAS_DIR/mysql"
    mkdir -p "$SAAS_DIR/redis"
    
    log_info "✓ Directories created"
}

create_workspace_compose() {
    log_info "Creating docker-compose for workspace..."
    
    cat > "$COMPOSE_DIR/docker-compose.yml" <<EOF
version: '3.8'

services:
  # MySQL Database
  mysql:
    image: mariadb:11
    container_name: thoth-workspace-mysql
    restart: unless-stopped
    environment:
      MYSQL_ROOT_PASSWORD: ${MYSQL_ROOT_PASS}
      MYSQL_DATABASE: owncloud
      MYSQL_USER: owncloud
      MYSQL_PASSWORD: ${MYSQL_OWNCLOUD_PASS}
    volumes:
      - mysql_data:/var/lib/mysql
    networks:
      - workspace_network
    healthcheck:
      test: ["CMD", "healthcheck.sh", "--connect", "--innodb_initialized"]
      interval: 10s
      timeout: 5s
      retries: 5

  # Redis Cache
  redis:
    image: redis:7-alpine
    container_name: thoth-workspace-redis
    restart: unless-stopped
    volumes:
      - redis_data:/data
    networks:
      - workspace_network
    healthcheck:
      test: ["CMD", "redis-cli", "ping"]
      interval: 10s
      timeout: 5s
      retries: 5

  # OwnCloud
  owncloud:
    image: owncloud/server:latest
    container_name: thoth-owncloud
    restart: unless-stopped
    depends_on:
      mysql:
        condition: service_healthy
      redis:
        condition: service_healthy
    environment:
      OWNCLOUD_DOMAIN: ${OWNCLOUD_DOMAIN}
      OWNCLOUD_TRUSTED_DOMAINS: ${OWNCLOUD_DOMAIN},localhost
      OWNCLOUD_DB_TYPE: mysql
      OWNCLOUD_DB_HOST: mysql
      OWNCLOUD_DB_NAME: owncloud
      OWNCLOUD_DB_USERNAME: owncloud
      OWNCLOUD_DB_PASSWORD: ${MYSQL_OWNCLOUD_PASS}
      OWNCLOUD_ADMIN_USERNAME: ${ADMIN_USER}
      OWNCLOUD_ADMIN_PASSWORD: ${ADMIN_PASS}
      OWNCLOUD_REDIS_ENABLED: "true"
      OWNCLOUD_REDIS_HOST: redis
    volumes:
      - owncloud_data:/mnt/data
    ports:
      - "8080:8080"
    networks:
      - workspace_network
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8080/status.php"]
      interval: 30s
      timeout: 10s
      retries: 5

  # OnlyOffice Document Server
  onlyoffice:
    image: onlyoffice/documentserver:latest
    container_name: thoth-onlyoffice
    restart: unless-stopped
    environment:
      JWT_ENABLED: "true"
      JWT_SECRET: "thoth_jwt_secret_2024"
      JWT_HEADER: "Authorization"
    volumes:
      - onlyoffice_data:/var/www/onlyoffice/Data
      - onlyoffice_log:/var/log/onlyoffice
    ports:
      - "8081:80"
    networks:
      - workspace_network
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost/healthcheck"]
      interval: 30s
      timeout: 10s
      retries: 5

volumes:
  mysql_data:
    driver: local
  redis_data:
    driver: local
  owncloud_data:
    driver: local
  onlyoffice_data:
    driver: local
  onlyoffice_log:
    driver: local

networks:
  workspace_network:
    driver: bridge
    name: thoth_workspace_network
EOF
    
    log_info "✓ Docker compose created"
}

save_credentials() {
    cat > "$SAAS_DIR/credentials.txt" <<EOF
========================================
THOTH Workspace Credentials
========================================

OwnCloud:
  URL: http://${OWNCLOUD_DOMAIN}:8080
  Admin User: ${ADMIN_USER}
  Admin Pass: ${ADMIN_PASS}

OnlyOffice:
  URL: http://${ONLYOFFICE_DOMAIN}:8081
  JWT Secret: thoth_jwt_secret_2024

MySQL:
  Root Password: ${MYSQL_ROOT_PASS}
  OwnCloud DB: owncloud
  OwnCloud User: owncloud
  OwnCloud Pass: ${MYSQL_OWNCLOUD_PASS}

Network: thoth_workspace_network

========================================
EOF
    
    chmod 600 "$SAAS_DIR/credentials.txt"
    log_info "✓ Credentials saved to $SAAS_DIR/credentials.txt"
}

start_services() {
    log_info "Starting workspace services..."
    
    cd "$COMPOSE_DIR"
    docker-compose up -d
    
    log_info "Waiting for services to be ready..."
    sleep 30
    
    # Check services
    for service in mysql redis owncloud onlyoffice; do
        if docker ps | grep -q "thoth-$service\|thoth-workspace-$service"; then
            log_info "✓ $service is running"
        else
            log_warn "$service may need more time"
        fi
    done
}

configure_owncloud_onlyoffice() {
    log_info "Configuring OwnCloud + OnlyOffice integration..."
    
    # Wait for OwnCloud to be fully ready
    sleep 15
    
    # Install OnlyOffice app in OwnCloud
    docker exec thoth-owncloud occ market:install onlyoffice || true
    docker exec thoth-owncloud occ app:enable onlyoffice || true
    
    # Configure OnlyOffice integration
    docker exec thoth-owncloud occ config:system:set \
        onlyoffice DocumentServerUrl --value="http://thoth-onlyoffice/"
    
    docker exec thoth-owncloud occ config:system:set \
        onlyoffice jwt_secret --value="thoth_jwt_secret_2024"
    
    log_info "✓ Integration configured"
}

create_billing_tracker() {
    log_info "Creating billing tracker..."
    
    cat > "$SAAS_DIR/billing-tracker.sh" <<'EOF'
#!/bin/bash

# Simple billing tracker for OwnCloud storage

OWNCLOUD_DATA="/var/lib/thoth-saas/owncloud"
BILLING_RATE=0.10  # $ per GB per month

calculate_storage() {
    if [ -d "$OWNCLOUD_DATA" ]; then
        USAGE_BYTES=$(du -sb "$OWNCLOUD_DATA" | cut -f1)
        USAGE_GB=$(echo "scale=2; $USAGE_BYTES / 1024 / 1024 / 1024" | bc)
        MONTHLY_COST=$(echo "scale=2; $USAGE_GB * $BILLING_RATE" | bc)
        
        echo "========================================"
        echo "OwnCloud Storage Billing"
        echo "========================================"
        echo "Storage Used: ${USAGE_GB} GB"
        echo "Rate: \$${BILLING_RATE} per GB/month"
        echo "Estimated Monthly Cost: \$${MONTHLY_COST}"
        echo "========================================"
        
        # Save to file for API access
        cat > "$OWNCLOUD_DATA/../billing.json" <<JSON
{
  "storage_gb": ${USAGE_GB},
  "rate_per_gb": ${BILLING_RATE},
  "monthly_cost": ${MONTHLY_COST},
  "timestamp": "$(date -Iseconds)"
}
JSON
    else
        echo "OwnCloud data directory not found"
        exit 1
    fi
}

case "$1" in
    calculate)
        calculate_storage
        ;;
    show)
        if [ -f "$OWNCLOUD_DATA/../billing.json" ]; then
            cat "$OWNCLOUD_DATA/../billing.json"
        else
            echo "No billing data available. Run: $0 calculate"
        fi
        ;;
    *)
        echo "Usage: $0 {calculate|show}"
        exit 1
        ;;
esac
EOF
    
    chmod +x "$SAAS_DIR/billing-tracker.sh"
    log_info "✓ Billing tracker created"
}

create_user_provisioning() {
    log_info "Creating user provisioning script..."
    
    cat > "$SAAS_DIR/provision-user.sh" <<'EOF'
#!/bin/bash

# Provision new user in OwnCloud workspace

if [ $# -ne 3 ]; then
    echo "Usage: $0 <username> <password> <email>"
    exit 1
fi

USERNAME=$1
PASSWORD=$2
EMAIL=$3

echo "Provisioning user: $USERNAME"

# Create user in OwnCloud
docker exec thoth-owncloud occ user:add "$USERNAME" --password-from-env --email="$EMAIL" <<< "$PASSWORD"

# Set storage quota (10GB default)
docker exec thoth-owncloud occ user:setting "$USERNAME" files quota "10GB"

echo "✓ User $USERNAME created with 10GB quota"
echo "  Access: http://cloud.thoth.local:8080"
echo "  Username: $USERNAME"
echo "  Password: $PASSWORD"
EOF
    
    chmod +x "$SAAS_DIR/provision-user.sh"
    log_info "✓ User provisioning script created"
}

show_summary() {
    log_info ""
    log_info "=========================================="
    log_info " THOTH Workspace Setup Complete!"
    log_info "=========================================="
    log_info ""
    log_info "🌐 OwnCloud:"
    log_info "   URL: http://${OWNCLOUD_DOMAIN}:8080"
    log_info "   Username: ${ADMIN_USER}"
    log_info "   Password: ${ADMIN_PASS}"
    log_info ""
    log_info "📄 OnlyOffice:"
    log_info "   URL: http://${ONLYOFFICE_DOMAIN}:8081"
    log_info "   (Integrated with OwnCloud)"
    log_info ""
    log_info "💰 Billing:"
    log_info "   Tracker: $SAAS_DIR/billing-tracker.sh"
    log_info "   Rate: \$0.10 per GB/month"
    log_info ""
    log_info "👥 User Management:"
    log_info "   Provision: $SAAS_DIR/provision-user.sh <user> <pass> <email>"
    log_info "   List users: docker exec thoth-owncloud occ user:list"
    log_info ""
    log_info "📊 Usage:"
    log_info "   Calculate billing: $SAAS_DIR/billing-tracker.sh calculate"
    log_info "   Show billing: $SAAS_DIR/billing-tracker.sh show"
    log_info ""
    log_info "All credentials saved to: $SAAS_DIR/credentials.txt"
    log_info ""
}

main() {
    log_info "THOTH Workspace - OwnCloud + OnlyOffice Setup"
    log_info "=============================================="
    
    check_requirements
    setup_directories
    create_workspace_compose
    save_credentials
    start_services
    configure_owncloud_onlyoffice
    create_billing_tracker
    create_user_provisioning
    show_summary
    
    log_info "✓ Setup complete!"
}

main "$@"