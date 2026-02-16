COSMO    ?= /home/groobiest/Code/jart/cosmopolitan/.cosmocc/3.9.2
CC       = $(COSMO)/bin/cosmocc
CFLAGS   = -Wall -Wextra -Werror -O2 -std=c11
BUILDDIR = build

SRCS = $(wildcard src/*.c)
OBJS = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(SRCS))

$(BUILDDIR)/wow.com: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILDDIR)/%.o: src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

.PHONY: clean
