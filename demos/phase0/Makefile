# Phase 0 demos — standalone, no project deps
-include ../../config.mk

COSMO   ?= /home/groobiest/Code/jart/cosmopolitan/.cosmocc/3.9.2
CC       = $(COSMO)/bin/cosmocc
CFLAGS   = -Wall -Wextra -Werror -O2 -std=c11

DEMOS = demo_tar.com demo_lockparse.com demo_compact_index.com demo_platform.com

all: $(DEMOS)

demo_tar.com: demo_tar.c
	$(CC) $(CFLAGS) -o $@ $<

demo_lockparse.com: demo_lockparse.c
	$(CC) $(CFLAGS) -o $@ $<

demo_compact_index.com: demo_compact_index.c
	$(CC) $(CFLAGS) -o $@ $<

demo_platform.com: demo_platform.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(DEMOS) *.com.dbg *.aarch64.elf

.PHONY: all clean
