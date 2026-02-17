# Phase 3 demos — parallel downloads (link against main project build)
-include ../../config.mk

COSMO     ?= /home/groobiest/Code/jart/cosmopolitan/.cosmocc/3.9.2
COSMO_SRC ?= /home/groobiest/Code/jart/cosmopolitan
CC         = $(COSMO)/bin/cosmocc
ZIPCOPY    = $(COSMO)/bin/zipcopy
CFLAGS     = -Wall -Wextra -Werror -O2 -std=c11 -D_COSMO_SOURCE -I$(COSMO_SRC)
BUILDDIR   = ../../build

# Objects from the main build needed by Phase 3 demos
HTTP_OBJS  = $(BUILDDIR)/http.o $(BUILDDIR)/pool.o $(BUILDDIR)/entropy.o
PROG_OBJS  = $(BUILDDIR)/progress.o $(BUILDDIR)/multibar.o $(BUILDDIR)/parallel.o
TLS_LIB    = $(BUILDDIR)/libtls.a
SSL_ZIP    = $(BUILDDIR)/ssl-roots.zip

DEMOS = demo_parallel.com

all: $(DEMOS)

demo_parallel.com: demo_parallel.c $(HTTP_OBJS) $(PROG_OBJS) $(TLS_LIB) $(SSL_ZIP)
	$(CC) $(CFLAGS) -I../../include -o $@ $< $(HTTP_OBJS) $(PROG_OBJS) $(TLS_LIB)
	$(ZIPCOPY) $(SSL_ZIP) $@

clean:
	rm -f $(DEMOS) *.com.dbg *.aarch64.elf

.PHONY: all clean
