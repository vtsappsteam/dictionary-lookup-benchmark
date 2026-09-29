CC = cc
CFLAGS = -O2 -Wall -Wextra -std=c11
LDFLAGS = -lm

# macOS ARM64 / x86_64 auto-detect
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_M),arm64)
    CFLAGS += -arch arm64
else ifeq ($(UNAME_M),x86_64)
    CFLAGS += -arch x86_64
endif

SRCS = benchmark.c hashmap.c sorted_array.c trie.c tst.c dawg.c bloom.c cuckoo.c cuckoo16.c
OBJS = $(SRCS:.c=.o)
TARGET = benchmark

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c structures.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
