#!/bin/bash

# ==============================================
# THOTH CLOUD - PaaS Shared Database Setup
# ==============================================
# This script sets up shared PostgreSQL and MariaDB
# instances for multi-tenant PaaS applications
#
# Features:
# - Single PostgreSQL instance for all PG apps
# - Single MariaDB instance for all MySQL apps
# - Logical separation per user/app
# - Automatic database/user creation
# ==============================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PAAS_DIR="/var/lib/thoth-paas"
COMPOSE_DIR="$PAAS_DIR/shared-databases"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check requirements
check_requirements() {
    log_info "Checking requirements..."
    
    if ! command -v docker &> /dev/null; then
        log_error "Docker is not installed"
        exit 1
    fi
    
    if ! command -v docker-compose &> /dev/null; then
        log_error "docker-compose is not installed"
        exit 1
    fi
    
    log_info "✓ All requirements met"
}

# Create directory structure
setup_directories() {
    log_info "Setting up directories..."
    
    mkdir -p "$PAAS_DIR"
    mkdir -p "$COMPOSE_DIR"
    mkdir -p "$PAAS_DIR/postgres-data"
    mkdir -p "$PAAS_DIR/mariadb-data"
    
    log_info "✓ Directories created"
}

# Generate docker-compose for shared databases
create_database_compose() {
    log_info "Creating docker-compose for shared databases..."
    
    cat > "$COMPOSE_DIR/docker-compose.yml" <<'EOF'
version: '3.8'

services:
  # Shared PostgreSQL Instance
  postgres:
    image: postgres:15-alpine
    container_name: thoth-postgres
    restart: unless-stopped
    environment:
      POSTGRES_PASSWORD: thoth_postgres_root_2024
      POSTGRES_USER: postgres
      PGDATA: /var/lib/postgresql/data/pgdata
    volumes:
      - postgres_data:/var/lib/postgresql/data
    ports:
      - "5432:5432"
    networks:
      - paas_network
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U postgres"]
      interval: 10s
      timeout: 5s
      retries: 5

  # Shared MariaDB Instance
  mariadb:
    image: mariadb:11
    container_name: thoth-mariadb
    restart: unless-stopped
    environment:
      MYSQL_ROOT_PASSWORD: thoth_mariadb_root_2024
    volumes:
      - mariadb_data:/var/lib/mysql
    ports:
      - "3306:3306"
    networks:
      - paas_network
    healthcheck:
      test: ["CMD", "healthcheck.sh", "--connect", "--innodb_initialized"]
      interval: 10s
      timeout: 5s
      retries: 5

volumes:
  postgres_data:
    driver: local
  mariadb_data:
    driver: local

networks:
  paas_network:
    driver: bridge
    name: thoth_paas_network
EOF
    
    log_info "✓ Docker compose file created"
}

# Create database management script
create_db_manager() {
    log_info "Creating database management script..."
    
    cat > "$PAAS_DIR/manage-databases.sh" <<'EOF'
#!/bin/bash

# Database Management Script for THOTH PaaS

POSTGRES_CONTAINER="thoth-postgres"
MARIADB_CONTAINER="thoth-mariadb"
POSTGRES_ROOT_PASS="thoth_postgres_root_2024"
MARIADB_ROOT_PASS="thoth_mariadb_root_2024"

case "$1" in
    create-pg)
        # Create PostgreSQL database and user
        # Usage: ./manage-databases.sh create-pg <username> <appname>
        USERNAME=$2
        APPNAME=$3
        DB_NAME="${USERNAME}_${APPNAME}"
        DB_USER="${USERNAME}_${APPNAME}_user"
        DB_PASS=$(openssl rand -base64 16)
        
        echo "Creating PostgreSQL database: $DB_NAME"
        
        docker exec -i $POSTGRES_CONTAINER psql -U postgres <<EOSQL
CREATE DATABASE ${DB_NAME};
CREATE USER ${DB_USER} WITH ENCRYPTED PASSWORD '${DB_PASS}';
GRANT ALL PRIVILEGES ON DATABASE ${DB_NAME} TO ${DB_USER};
EOSQL
        
        echo "✓ Database created successfully"
        echo "Connection details:"
        echo "  Host: localhost (or thoth-postgres from containers)"
        echo "  Port: 5432"
        echo "  Database: $DB_NAME"
        echo "  User: $DB_USER"
        echo "  Password: $DB_PASS"
        
        # Save credentials
        echo "$DB_NAME|$DB_USER|$DB_PASS" >> /var/lib/thoth-paas/pg-credentials.txt
        ;;
        
    create-mysql)
        # Create MySQL/MariaDB database and user
        # Usage: ./manage-databases.sh create-mysql <username> <appname>
        USERNAME=$2
        APPNAME=$3
        DB_NAME="${USERNAME}_${APPNAME}"
        DB_USER="${USERNAME}_${APPNAME}_user"
        DB_PASS=$(openssl rand -base64 16)
        
        echo "Creating MariaDB database: $DB_NAME"
        
        docker exec -i $MARIADB_CONTAINER mysql -uroot -p${MARIADB_ROOT_PASS} <<EOSQL
CREATE DATABASE ${DB_NAME} CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER '${DB_USER}'@'%' IDENTIFIED BY '${DB_PASS}';
GRANT ALL PRIVILEGES ON ${DB_NAME}.* TO '${DB_USER}'@'%';
FLUSH PRIVILEGES;
EOSQL
        
        echo "✓ Database created successfully"
        echo "Connection details:"
        echo "  Host: localhost (or thoth-mariadb from containers)"
        echo "  Port: 3306"
        echo "  Database: $DB_NAME"
        echo "  User: $DB_USER"
        echo "  Password: $DB_PASS"
        
        # Save credentials
        echo "$DB_NAME|$DB_USER|$DB_PASS" >> /var/lib/thoth-paas/mysql-credentials.txt
        ;;
        
    list-pg)
        echo "PostgreSQL Databases:"
        docker exec $POSTGRES_CONTAINER psql -U postgres -c "\l"
        ;;
        
    list-mysql)
        echo "MariaDB Databases:"
        docker exec $MARIADB_CONTAINER mysql -uroot -p${MARIADB_ROOT_PASS} -e "SHOW DATABASES;"
        ;;
        
    delete-pg)
        # Delete PostgreSQL database
        DB_NAME=$2
        echo "Deleting PostgreSQL database: $DB_NAME"
        docker exec $POSTGRES_CONTAINER psql -U postgres -c "DROP DATABASE IF EXISTS ${DB_NAME};"
        docker exec $POSTGRES_CONTAINER psql -U postgres -c "DROP USER IF EXISTS ${DB_NAME}_user;"
        echo "✓ Database deleted"
        ;;
        
    delete-mysql)
        # Delete MySQL database
        DB_NAME=$2
        echo "Deleting MariaDB database: $DB_NAME"
        docker exec $MARIADB_CONTAINER mysql -uroot -p${MARIADB_ROOT_PASS} -e "DROP DATABASE IF EXISTS ${DB_NAME};"
        docker exec $MARIADB_CONTAINER mysql -uroot -p${MARIADB_ROOT_PASS} -e "DROP USER IF EXISTS '${DB_NAME}_user'@'%';"
        echo "✓ Database deleted"
        ;;
        
    *)
        echo "Usage:"
        echo "  $0 create-pg <username> <appname>    - Create PostgreSQL database"
        echo "  $0 create-mysql <username> <appname> - Create MariaDB database"
        echo "  $0 list-pg                           - List PostgreSQL databases"
        echo "  $0 list-mysql                        - List MariaDB databases"
        echo "  $0 delete-pg <dbname>                - Delete PostgreSQL database"
        echo "  $0 delete-mysql <dbname>             - Delete MariaDB database"
        exit 1
        ;;
esac
EOF
    
    chmod +x "$PAAS_DIR/manage-databases.sh"
    log_info "✓ Database manager created at $PAAS_DIR/manage-databases.sh"
}

# Start shared databases
start_databases() {
    log_info "Starting shared database containers..."
    
    cd "$COMPOSE_DIR"
    docker-compose up -d
    
    log_info "Waiting for databases to be ready..."
    sleep 15
    
    # Check PostgreSQL
    if docker exec thoth-postgres pg_isready -U postgres &> /dev/null; then
        log_info "✓ PostgreSQL is ready"
    else
        log_warn "PostgreSQL might need more time to initialize"
    fi
    
    # Check MariaDB
    if docker exec thoth-mariadb mysqladmin ping -h localhost -uroot -pthoth_mariadb_root_2024 &> /dev/null; then
        log_info "✓ MariaDB is ready"
    else
        log_warn "MariaDB might need more time to initialize"
    fi
}

# Display summary
show_summary() {
    log_info ""
    log_info "========================================="
    log_info "  Shared Database Setup Complete!"
    log_info "========================================="
    log_info ""
    log_info "PostgreSQL:"
    log_info "  Container: thoth-postgres"
    log_info "  Port: 5432"
    log_info "  Root Password: thoth_postgres_root_2024"
    log_info ""
    log_info "MariaDB:"
    log_info "  Container: thoth-mariadb"
    log_info "  Port: 3306"
    log_info "  Root Password: thoth_mariadb_root_2024"
    log_info ""
    log_info "Database Manager: $PAAS_DIR/manage-databases.sh"
    log_info ""
    log_info "Example usage:"
    log_info "  # Create database for user 'john' app 'wordpress'"
    log_info "  $PAAS_DIR/manage-databases.sh create-mysql john wordpress"
    log_info ""
    log_info "  # List all databases"
    log_info "  $PAAS_DIR/manage-databases.sh list-mysql"
    log_info "  $PAAS_DIR/manage-databases.sh list-pg"
    log_info ""
}

# Main execution
main() {
    log_info "THOTH Cloud - PaaS Shared Database Setup"
    log_info "=========================================="
    
    check_requirements
    setup_directories
    create_database_compose
    create_db_manager
    start_databases
    show_summary
    
    log_info "✓ Setup complete!"
}

main "$@"