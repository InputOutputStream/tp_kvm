CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra 
LDFLAGS = -lvirt -lpthread -lssl -lcrypto


# Directories
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

# Target executable
TARGET = $(BIN_DIR)/server

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

# Default target
all: $(TARGET)

# Create directories
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Link
$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

setup:
	@echo " Configuration du projet..."
	@chmod +x setup-libs.sh
	@./setup-libs.sh

# Exécuter en mode debug
debug: CXXFLAGS += -g -DDEBUG
debug: clean $(TARGET)
	@echo " Mode debug activé"
	sudo gdb ./bin/$(TARGET)

# Installation des dépendances système
install-deps:
	@echo " Installation des dépendances système..."
	sudo apt update
	sudo apt install -y libvirt-dev libvirt-daemon-system g++ make git

# Recompilation complète
rebuild: clean all


# Compile
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Vérification de la syntaxe
check:
	@echo "🔍 Vérification de la syntaxe..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fsyntax-only $(SRC)
	@echo " Syntaxe valide"


# Afficher les informations du système
info:
	@echo " Informations système:"
	@echo "  Compilateur: $(CXX) $(shell $(CXX) --version | head -n1)"
	@echo "  Flags: $(CXXFLAGS)"
	@echo "  Libs: $(LIBS)"
	@echo ""
	@echo " État des dépendances:"
	@if [ -f "include/httplib.h" ]; then echo "   httplib.h présent"; else echo "   httplib.h manquant"; fi
	@if [ -f "include/json.hpp" ]; then echo "   json.hpp présent"; else echo "   json.hpp manquant"; fi
	@if dpkg -l | grep -q libvirt-dev; then echo "  libvirt-dev installé"; else echo "   libvirt-dev manquant"; fi


# Clean
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Clean complete"

# Run
run: $(TARGET)
	./$(TARGET)

# Rebuild
rebuild: clean all

# Test de compilation rapide
test-compile:
	@echo "⚡ Test de compilation rapide..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $(SRC) -o /tmp/test.o
	@rm -f /tmp/test.o
	@echo " Test réussi"


# Help
help:
	@echo "Available targets:"
	@echo "  all          - Build the project (default)"
	@echo "  clean        - Remove build files"
	@echo "  run          - Build and run the server"
	@echo "  rebuild      - Clean and rebuild"
	@echo "  install-deps - Install required dependencies"
	@echo "  help         - Show this help message"

.PHONY: all clean run rebuild install-deps help
