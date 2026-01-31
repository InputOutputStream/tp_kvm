#!/bin/bash

# Configuration
TARGET_DIR="/var/lib/thoth-cloud"
USER="thot"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Setting permissions for ${USER} on ${TARGET_DIR}${NC}"

# Check if script is run as root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}Please run as root or with sudo${NC}"
    exit 1
fi

# Check if user exists
if ! id "$USER" &>/dev/null; then
    echo -e "${RED}User ${USER} does not exist${NC}"
    exit 1
fi

# Create directory if it doesn't exist
if [ ! -d "$TARGET_DIR" ]; then
    echo -e "${YELLOW}Creating directory ${TARGET_DIR}${NC}"
    mkdir -p "$TARGET_DIR"
fi

# Set ownership to vps user and group
echo -e "${YELLOW}Setting ownership...${NC}"
chown -R ${USER}:${USER} "$TARGET_DIR"

# Set permissions: rwx for user, rx for group, rx for others
# or use 755 for more restrictive (rwxr-xr-x)
echo -e "${YELLOW}Setting permissions...${NC}"
chmod -R 755 "$TARGET_DIR"

# Give the user full permissions (rwx) on their own files
# This allows read, write, and execute on all files/subdirectories
find "$TARGET_DIR" -type d -exec chmod 755 {} \;
find "$TARGET_DIR" -type f -exec chmod 644 {} \;

# Set ACL for more granular control (requires acl package)
if command -v setfacl &> /dev/null; then
    echo -e "${YELLOW}Setting ACL permissions...${NC}"
    setfacl -R -m u:${USER}:rwx "$TARGET_DIR"
    setfacl -R -d -m u:${USER}:rwx "$TARGET_DIR"  # Default ACL for new files
fi

echo -e "${GREEN}Permissions set successfully!${NC}"
echo -e "Owner: ${USER}:${USER}"
echo -e "Directory: ${TARGET_DIR}"
ls -la "$TARGET_DIR"
