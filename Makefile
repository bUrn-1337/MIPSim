CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -g -Isrc

MIPS_CC    = mips-linux-gnu-gcc
MIPS_FLAGS = -static -mips32 -nostdlib -fno-stack-protector -no-pie -Wl,-e,_start

SRCS   = src/main.c src/cpu.c src/memory.c src/decoder.c src/elf_loader.c src/predictor.c
OBJS   = $(SRCS:.c=.o)
TARGET = mipsim

TESTS  = tests/hello tests/loop tests/random_branch

.PHONY: all clean test bench trace

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

tests/%: tests/%.c
	$(MIPS_CC) $(MIPS_FLAGS) -o $@ $<

# Build all test binaries and run each under the 2-bit predictor
test: $(TARGET) $(TESTS)
	@for t in $(TESTS); do \
	    echo "--- $$t ---"; \
	    ./$(TARGET) --predictor=2bit $$t 2>&1; \
	    echo ""; \
	done

# Print every instruction as it executes for tests/hello
trace: $(TARGET) tests/hello
	./$(TARGET) --trace tests/hello 2>&1

# Run all three tests under all three predictors (resume numbers)
bench: $(TARGET) $(TESTS)
	@for t in $(TESTS); do \
	    echo ""; \
	    echo "========================================"; \
	    echo "  $$t"; \
	    echo "========================================"; \
	    for p in static 1bit 2bit; do \
	        echo "  [--predictor=$$p]"; \
	        ./$(TARGET) --predictor=$$p $$t 2>&1 | grep -E "(Branches|Correct|Incorrect|Predictor)"; \
	        echo ""; \
	    done; \
	done

clean:
	rm -f $(OBJS) $(TARGET) $(TESTS)
