# Makefile pour le serveur libvirt C++

CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2
INCLUDES = -Iinclude
LIBS = -lvirt -lpthread

SRC = server.cpp
TARGET = build/libvirt_server
BUILD_DIR = build

.PHONY: all clean run setup help install-deps

# Compilation par défaut
all: $(TARGET)

# Compilation du serveur
$(TARGET): $(SRC)
	@echo "🔨 Compilation du serveur..."
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $(TARGET) $(SRC) $(LIBS)
	@echo "✅ Compilation terminée : $(TARGET)"

# Setup initial (télécharge et configure les dépendances)
setup:
	@echo "🚀 Configuration du projet..."
	@chmod +x setup.sh
	@./setup.sh

# Installation des dépendances système
install-deps:
	@echo "📦 Installation des dépendances système..."
	sudo apt update
	sudo apt install -y libvirt-dev libvirt-daemon-system g++ make git

# Exécuter le serveur (nécessite sudo)
run: $(TARGET)
	@echo "🚀 Démarrage du serveur..."
	sudo ./$(TARGET)

# Exécuter en mode debug
debug: CXXFLAGS += -g -DDEBUG
debug: clean $(TARGET)
	@echo "🐛 Mode debug activé"
	sudo gdb ./$(TARGET)

# Nettoyage
clean:
	@echo "🧹 Nettoyage..."
	rm -rf $(BUILD_DIR)
	rm -f server.cpp
	@echo "✅ Nettoyage terminé"

# Nettoyage complet (inclut les dépendances)
distclean: clean
	@echo "🧹 Nettoyage complet..."
	rm -rf dependencies include
	@echo "✅ Nettoyage complet terminé"

# Vérification de la syntaxe
check:
	@echo "🔍 Vérification de la syntaxe..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fsyntax-only $(SRC)
	@echo "✅ Syntaxe valide"

# Test de compilation rapide
test-compile:
	@echo "⚡ Test de compilation rapide..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $(SRC) -o /tmp/test.o
	@rm -f /tmp/test.o
	@echo "✅ Test réussi"

# Afficher les informations du système
info:
	@echo "📊 Informations système:"
	@echo "  Compilateur: $(CXX) $(shell $(CXX) --version | head -n1)"
	@echo "  Flags: $(CXXFLAGS)"
	@echo "  Libs: $(LIBS)"
	@echo ""
	@echo "📦 État des dépendances:"
	@if [ -f "include/httplib.h" ]; then echo "  ✅ httplib.h présent"; else echo "  ❌ httplib.h manquant"; fi
	@if [ -f "include/json.hpp" ]; then echo "  ✅ json.hpp présent"; else echo "  ❌ json.hpp manquant"; fi
	@if dpkg -l | grep -q libvirt-dev; then echo "  ✅ libvirt-dev installé"; else echo "  ❌ libvirt-dev manquant"; fi

# Recompilation complète
rebuild: clean all

# Aide
help:
	@echo "🔧 Commandes disponibles:"
	@echo ""
	@echo "  make              - Compiler le serveur"
	@echo "  make setup        - Configuration initiale (première fois)"
	@echo "  make install-deps - Installer les dépendances système"
	@echo "  make run          - Compiler et exécuter le serveur"
	@echo "  make debug        - Compiler en mode debug"
	@echo "  make clean        - Nettoyer les fichiers compilés"
	@echo "  make distclean    - Nettoyage complet"
	@echo "  make rebuild      - Recompilation complète"
	@echo "  make check        - Vérifier la syntaxe"
	@echo "  make info         - Afficher les informations système"
	@echo "  make help         - Afficher cette aide"
	@echo ""
	@echo "📝 Workflow recommandé:"
	@echo "  1. make install-deps  (une seule fois)"
	@echo "  2. make setup         (une seule fois)"
	@echo "  3. make run           (pour compiler et lancer)"