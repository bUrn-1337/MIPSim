CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -Isrc

SRCS   = src/main.c src/cpu.c src/memory.c src/decoder.c
OBJS   = $(SRCS:.c=.o)
TARGET = mipsim

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
