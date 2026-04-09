CC = gcc
CFLAGS = -g3 -O0 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wnull-dereference -Wsign-conversion -Wformat=2
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer -fstack-protector-strong
LDFLAGS = -lreadline -lelf -ldw
TARGET = vdbg
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) *.o
