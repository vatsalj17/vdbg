CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnull-dereference -Wsign-conversion -Wformat=2
LDFLAGS = -lreadline -lelf -ldw

DEV_CFLAGS = $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer -fstack-protector-strong
DEV_LDFLAGS = $(LDFLAGS) -fsanitize=address,undefined

DEBUG_CFLAGS = $(DEV_CFLAGS) -g3 -O0 -DDEBUG

BUILD_CFLAGS = $(CFLAGS) -O2 -s -fstack-protector-strong -fPIE
BUILD_LDFLAGS = $(LDFLAGS) -pie -Wl,-z,relro,-z,now

TARGET = vdbg
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

SRC = $(wildcard $(SRC_DIR)/*.c)
OBJ = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))

all: ACTIVE_CFLAGS = $(DEV_CFLAGS)
all: ACTIVE_LDFLAGS = $(DEV_LDFLAGS)
all: $(OBJ_DIR) $(TARGET)

debug: ACTIVE_CFLAGS = $(DEBUG_CFLAGS)
debug: ACTIVE_LDFLAGS = $(DEV_LDFLAGS)
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

clean:
	rm -rf $(TARGET) $(OBJ_DIR)
