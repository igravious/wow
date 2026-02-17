# Phase 2 demos — link against main project build objects
-include ../../config.mk

COSMO     ?= /home/groobiest/Code/jart/cosmopolitan/.cosmocc/3.9.2
COSMO_SRC ?= /home/groobiest/Code/jart/cosmopolitan
CC         = $(COSMO)/bin/cosmocc
ZIPCOPY    = $(COSMO)/bin/zipcopy
CFLAGS     = -Wall -Wextra -Werror -O2 -std=c11 -D_COSMO_SOURCE -I$(COSMO_SRC)
BUILDDIR   = ../../build

# Objects from the main build needed by Phase 2 demos
HTTP_OBJS  = $(BUILDDIR)/http.o $(BUILDDIR)/pool.o $(BUILDDIR)/entropy.o
REG_OBJS   = $(BUILDDIR)/registry.o $(BUILDDIR)/cJSON.o
TLS_LIB    = $(BUILDDIR)/libtls.a
SSL_ZIP    = $(BUILDDIR)/ssl-roots.zip

DEMOS = demo_https.com demo_gem_info.com

all: $(DEMOS)

demo_https.com: demo_https.c $(HTTP_OBJS) $(TLS_LIB) $(SSL_ZIP)
	$(CC) $(CFLAGS) -I../../include -o $@ $< $(HTTP_OBJS) $(TLS_LIB)
	$(ZIPCOPY) $(SSL_ZIP) $@

demo_gem_info.com: demo_gem_info.c $(HTTP_OBJS) $(REG_OBJS) $(TLS_LIB) $(SSL_ZIP)
	$(CC) $(CFLAGS) -I../../include -I../../vendor/cjson -o $@ $< $(HTTP_OBJS) $(REG_OBJS) $(TLS_LIB)
	$(ZIPCOPY) $(SSL_ZIP) $@

clean:
	rm -f $(DEMOS) *.com.dbg *.aarch64.elf

.PHONY: all clean
