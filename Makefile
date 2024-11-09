CC = gcc
CFLAGS = -Wall -pthread

# Directories
CLIENT_DIR = src/client
NAMING_SERVER_DIR = src/naming_server
STORAGE_SERVER_DIR = src/storage_server
COMMON_DIR = src/common

# Sources
CLIENT_SRC = $(CLIENT_DIR)/client.c
NAMING_SERVER_SRC = $(NAMING_SERVER_DIR)/naming_server.c
STORAGE_SERVER_SRC = $(STORAGE_SERVER_DIR)/storage_server.c

# Binaries
CLIENT_BIN = client
NAMING_SERVER_BIN = naming_server
STORAGE_SERVER_BIN = storage_server

# Default target
all: $(CLIENT_BIN) $(NAMING_SERVER_BIN) $(STORAGE_SERVER_BIN)

# Build client
$(CLIENT_BIN): $(CLIENT_SRC) $(COMMON_DIR)/protocol.h
	$(CC) $(CFLAGS) -o $(CLIENT_BIN) $(CLIENT_SRC)

# Build naming_server
$(NAMING_SERVER_BIN): $(NAMING_SERVER_SRC) $(COMMON_DIR)/protocol.h
	$(CC) $(CFLAGS) -o $(NAMING_SERVER_BIN) $(NAMING_SERVER_SRC)

# Build storage_server
$(STORAGE_SERVER_BIN): $(STORAGE_SERVER_SRC) $(COMMON_DIR)/protocol.h
	$(CC) $(CFLAGS) -o $(STORAGE_SERVER_BIN) $(STORAGE_SERVER_SRC)

# Clean
clean:
	rm -f $(CLIENT_BIN) $(NAMING_SERVER_BIN) $(STORAGE_SERVER_BIN)