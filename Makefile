CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -std=c11 -O2 -g
LDFLAGS ?= -lm
SRC     := hash.c avl.c heap.c queue.c stack.c game.c anim.c bench.c platform.c main.c
OBJ     := $(SRC:.c=.o)

all: arena
arena: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)
%.o: %.c arena.h
	$(CC) $(CFLAGS) -c $< -o $@
test: arena
	./arena --selftest
bench: arena
	./arena --bench
demo: arena
	./arena --demo 120
# Leak/memory check (valgrind if present, otherwise GCC's AddressSanitizer + LeakSanitizer)
memcheck:
	@if command -v valgrind >/dev/null; then \
	  $(MAKE) -s clean arena CFLAGS="-Wall -Wextra -std=c11 -O1 -g"; \
	  valgrind --leak-check=full --error-exitcode=1 ./arena --selftest; \
	else \
	  $(MAKE) -s clean arena CFLAGS="-Wall -Wextra -std=c11 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"; \
	  ./arena --selftest; \
	fi
clean:
	rm -f $(OBJ) arena
.PHONY: all test bench demo memcheck clean
