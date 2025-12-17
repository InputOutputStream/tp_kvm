#!/bin/bash

set -e  # Arrêter en cas d'erreur

echo "Configuration du serveur libvirt C++..."
echo ""

# Couleurs pour l'output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Répertoires
PROJECT_DIR="$(pwd)"
DEPS_DIR="$PROJECT_DIR/dependencies"
INCLUDE_DIR="$PROJECT_DIR/include"

echo -e "${BLUE}Création de la structure de répertoires...${NC}"
mkdir -p "$DEPS_DIR"
mkdir -p "$INCLUDE_DIR"
mkdir -p "$PROJECT_DIR/build"

# ========== VÉRIFICATION DES DÉPENDANCES SYSTÈME ==========
echo ""
echo -e "${BLUE} Vérification des dépendances système...${NC}"

if ! command -v g++ &> /dev/null; then
    echo -e "${RED} g++ n'est pas installé${NC}"
    echo "Installez-le avec: sudo apt install g++"
    exit 1
fi

if ! dpkg -l | grep -q libvirt-dev; then
    echo -e "${YELLOW} libvirt-dev n'est pas installé${NC}"
    echo "Installation de libvirt-dev..."
    sudo apt update
    sudo apt install -y libvirt-dev libvirt-daemon-system
else
    echo -e "${GREEN} libvirt-dev est installé${NC}"
fi

# ========== CPP-HTTPLIB ==========
echo ""
echo -e "${BLUE} Configuration de cpp-httplib...${NC}"

if [ ! -f "$INCLUDE_DIR/httplib.h" ]; then
    if [ ! -d "$DEPS_DIR/cpp-httplib" ]; then
        echo "Clonage de cpp-httplib..."
        cd "$DEPS_DIR"
        git clone https://github.com/yhirose/cpp-httplib.git
    fi
    
    echo "Copie de httplib.h..."
    cp "$DEPS_DIR/cpp-httplib/httplib.h" "$INCLUDE_DIR/"
    echo -e "${GREEN} httplib.h installé${NC}"
else
    echo -e "${GREEN} httplib.h déjà présent${NC}"
fi

# ========== NLOHMANN JSON ==========
echo ""
echo -e "${BLUE} Configuration de nlohmann/json...${NC}"

if [ ! -f "$INCLUDE_DIR/json.hpp" ]; then
    if [ ! -d "$DEPS_DIR/json" ]; then
        echo "Clonage de nlohmann/json..."
        cd "$DEPS_DIR"
        git clone https://github.com/nlohmann/json.git
    fi
    
    echo "Copie de json.hpp..."
    cp "$DEPS_DIR/json/single_include/nlohmann/json.hpp" "$INCLUDE_DIR/"
    echo -e "${GREEN} json.hpp installé${NC}"
else
    echo -e "${GREEN} json.hpp déjà présent${NC}"
fi

