# Phase 6 demos — PubGrub dependency resolver
# Demonstrates version parsing, constraint matching, and resolution
-include ../../config.mk

COSMO     ?= /home/groobiest/Code/jart/cosmopolitan/.cosmocc/3.9.2
COSMO_SRC ?= /home/groobiest/Code/jart/cosmopolitan
CC         = $(COSMO)/bin/cosmocc
ZIPCOPY    = $(COSMO)/bin/zipcopy
CFLAGS     = -Wall -Wextra -Werror -O2 -std=c17 -D_COSMO_SOURCE -I$(COSMO_SRC) -I../../vendor
BUILDDIR   = ../../build

# All objects except main.o for linking demos
ALL_OBJS = $(filter-out $(BUILDDIR)/main.o,$(wildcard $(BUILDDIR)/*.o) \
           $(wildcard $(BUILDDIR)/http/*.o) \
           $(wildcard $(BUILDDIR)/download/*.o) \
           $(wildcard $(BUILDDIR)/rubies/*.o) \
           $(wildcard $(BUILDDIR)/gems/*.o) \
           $(wildcard $(BUILDDIR)/gemfile/*.o) \
           $(wildcard $(BUILDDIR)/pubgrub/*.o) \
           $(wildcard $(BUILDDIR)/util/*.o))
TLS_LIB    = $(BUILDDIR)/libtls.a
YAML_LIB   = $(BUILDDIR)/libyaml.a
ASSETS_ZIP = $(BUILDDIR)/assets.zip

DEMOS = demo_version.com demo_pubgrub.com demo_resolve.com demo_lock.com

all: $(DEMOS)

demo_version.com: demo_version.c $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB) $(SSL_ZIP)
	$(CC) $(CFLAGS) -I../../include -o $@ $< $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

demo_pubgrub.com: demo_pubgrub.c $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB) $(SSL_ZIP)
	$(CC) $(CFLAGS) -I../../include -o $@ $< $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

demo_resolve.com: demo_resolve.c $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB) $(SSL_ZIP)
	$(CC) $(CFLAGS) -I../../include -o $@ $< $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

demo_lock.com: demo_lock.c $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB) $(SSL_ZIP)
	$(CC) $(CFLAGS) -I../../include -o $@ $< $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

clean:
	rm -f $(DEMOS) *.com.dbg *.aarch64.elf

.PHONY: all clean
