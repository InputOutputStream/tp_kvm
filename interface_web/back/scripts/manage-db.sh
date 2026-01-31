#!/bin/bash

# going to Create /var/lib/thoth-paas/manage-databases.sh:

ACTION=$1
USERNAME=$2
APPNAME=$3

DB_NAME="${USERNAME}_${APPNAME}"
DB_USER="${USERNAME}_${APPNAME}_user"
DB_PASS=$(openssl rand -base64 16)

case $ACTION in
    create-pg)
        # PostgreSQL
        docker exec thoth-postgres psql -U postgres -c "CREATE DATABASE $DB_NAME;"
        docker exec thoth-postgres psql -U postgres -c "CREATE USER $DB_USER WITH PASSWORD '$DB_PASS';"
        docker exec thoth-postgres psql -U postgres -c "GRANT ALL PRIVILEGES ON DATABASE $DB_NAME TO $DB_USER;"
        
        echo "thoth-postgres"
        echo "5432"
        echo "$DB_NAME"
        echo "$DB_USER"
        echo "$DB_PASS"
        ;;
        
    create-mysql)
        # MariaDB
        docker exec thoth-mariadb mysql -u root -p$MYSQL_ROOT_PASSWORD -e "CREATE DATABASE $DB_NAME;"
        docker exec thoth-mariadb mysql -u root -p$MYSQL_ROOT_PASSWORD -e "CREATE USER '$DB_USER'@'%' IDENTIFIED BY '$DB_PASS';"
        docker exec thoth-mariadb mysql -u root -p$MYSQL_ROOT_PASSWORD -e "GRANT ALL PRIVILEGES ON $DB_NAME.* TO '$DB_USER'@'%';"
        docker exec thoth-mariadb mysql -u root -p$MYSQL_ROOT_PASSWORD -e "FLUSH PRIVILEGES;"
        
        echo "thoth-mariadb"
        echo "3306"
        echo "$DB_NAME"
        echo "$DB_USER"
        echo "$DB_PASS"
        ;;
esac