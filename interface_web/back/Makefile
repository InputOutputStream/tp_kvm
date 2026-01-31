CXX = g++
CXXFLAGS = -g -std=c++17 -Wall -Wextra  
LDFLAGS = -lvirt -lpthread -lssl -lcrypto

# Directories
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

# Target executables
TARGET = $(BIN_DIR)/server
ADMIN_TARGET = $(BIN_DIR)/create_admin

ALL_SRCS = $(wildcard $(SRC_DIR)/*.cpp)

SOURCES = $(filter-out $(SRC_DIR)/create_admin.cpp, $(ALL_SRCS))

ALL_OBJECTS = $(SOURCES:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

OBJECTS = $(filter-out $(OBJ_DIR)/create_admin.o, $(ALL_OBJECTS))

# Default target
all: $(TARGET)

# Create directories
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Link Main Server
$(TARGET): $(OBJECTS) | $(BIN_DIR)
	@echo "Linking Server..."
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

# Generic Compile Rule (for src files)
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Utility targets
setup:
	@echo " Configuration du projet..."
	@chmod +x setup-libs.sh
	@./setup-libs.sh

debug: CXXFLAGS += -g -DDEBUG
debug: clean $(TARGET)
	@echo " Mode debug activé"
	sudo gdb ./bin/$(TARGET)

install-deps:
	@echo " Installation des dépendances système..."
	sudo apt update
	sudo apt install -y libvirt-dev libvirt-daemon-system g++ make git

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)
	@echo "Clean complete"

run: $(TARGET)
	./$(TARGET)

rebuild: clean all

# Explicitly defining create_admin target alias
create_admin: $(ADMIN_TARGET)

# Link Admin Tool
# 1. We compile create_admin.cpp directly.
# 2. We link it with $(OBJECTS) (your backend logic).
# 3. We MUST filter out main.o from $(OBJECTS) so we don't have two main() functions.
$(ADMIN_TARGET): $(SRC_DIR)/create_admin.cpp $(OBJECTS) | $(BIN_DIR)
	@echo "Creating admin executable..."
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) $(SRC_DIR)/create_admin.cpp $(filter-out $(OBJ_DIR)/main.o, $(OBJECTS)) -o $@ $(LDFLAGS)
	@echo "Done. Run with: ./$(ADMIN_TARGET)"

test-compile:
	@echo "⚡ Test de compilation rapide..."
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c $(SRC_DIR)/main.cpp -o /tmp/test.o
	@rm -f /tmp/test.o
	@echo " Test réussi"

info:
	@echo " Source Files (Server): $(SOURCES)"
	@echo " Object Files (Server): $(OBJECTS)"

.PHONY: all clean run rebuild install-deps help create_admin