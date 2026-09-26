CC = gcc
CFLAGS = -Wall -O2 -std=c11
LDFLAGS = -ldl

TARGET = ../../build/inject_linux

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
