#!/bin/bash

# Couleurs
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}=== serveur libvirt C++ ===${NC}"
echo ""

# Arrêter le serveur s'il tourne
if pgrep -f "libvirt_server" > /dev/null; then
    echo -e "${BLUE}Arrêt du serveur existant...${NC}"
    sudo pkill -f "libvirt_server"
    sleep 1
fi

# Nettoyer
echo -e "${BLUE}Nettoyage...${NC}"
rm -rf build/

# Compiler
echo -e "${BLUE}Compilation...${NC}"
mkdir -p build
g++ -std=c++17 -Wall -O2 -Iinclude -o build/libvirt_server server.cpp -lvirt -lpthread

if [ $? -eq 0 ]; then
    echo ""
    echo -e "${GREEN} Compilation réussie!${NC}"
    echo ""
    echo -e "${BLUE}Pour lancer le serveur:${NC}"
    echo -e "  ${GREEN}sudo ./build/libvirt_server${NC}"
    echo ""
    echo -e "${BLUE}Ou avec make:${NC}"
    echo -e "  ${GREEN}make run${NC}"
else
    echo ""
    echo -e "${RED}✗ Erreur de compilation${NC}"
    exit 1
fi