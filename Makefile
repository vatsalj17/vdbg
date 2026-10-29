CC = gcc
CFLAGS = -std=c23 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
CFLAGS += -Wall -Wextra -Wshadow -Wconversion -Wnull-dereference -Wsign-conversion -Wformat=2
LDFLAGS = -lreadline -lelf -ldw -lcapstone

DEV_CFLAGS = $(CFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer -fstack-protector-strong
DEV_LDFLAGS = $(LDFLAGS) -fsanitize=address,undefined

DEBUG_CFLAGS = $(CFLAGS) -g3 -O0 -DDEBUG -fno-omit-frame-pointer

BUILD_CFLAGS = $(CFLAGS) -O2 -s -fstack-protector-strong -fPIE -flto
BUILD_LDFLAGS = $(LDFLAGS) -pie -Wl,-z,relro,-z,now -flto

TARGET = vdbg
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

PREFIX ?= /usr/local
BIN_DIR = $(PREFIX)/bin
COMPLETION_DIR = $(PREFIX)/share/bash-completion/completions
COMPLETION_SRC = completions/vdbg_comp.bash

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))

all: ACTIVE_CFLAGS = $(DEV_CFLAGS)
all: ACTIVE_LDFLAGS = $(DEV_LDFLAGS)
all: $(OBJ_DIR) $(TARGET)

debug: ACTIVE_CFLAGS = $(DEBUG_CFLAGS)
debug: ACTIVE_LDFLAGS = $(LDFLAGS)
debug: $(OBJ_DIR) $(TARGET)

build: ACTIVE_CFLAGS = $(BUILD_CFLAGS)
build: ACTIVE_LDFLAGS = $(BUILD_LDFLAGS)
build: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJ)
	$(CC) $(ACTIVE_CFLAGS) -o $@ $^ $(ACTIVE_LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(ACTIVE_CFLAGS) -I$(INC_DIR) -c $< -o $@

install: build
	sudo cp $(TARGET) $(BIN_DIR)/
	sudo mkdir -p $(COMPLETION_DIR)
	sudo cp $(COMPLETION_SRC) $(COMPLETION_DIR)/$(TARGET)

uninstall:
	sudo rm -f $(BIN_DIR)/$(TARGET)
	sudo rm -f $(COMPLETION_DIR)/$(TARGET)

clean:
	rm -rf $(TARGET) $(OBJ_DIR)
