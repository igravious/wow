# Phase 8 demos — End-to-End sync and run
# Demonstrates wow sync pipeline and wow run environment setup
-include ../../config.mk

COSMO     ?= /home/groobiest/Code/jart/cosmopolitan/.cosmocc/3.9.2
COSMO_SRC ?= /home/groobiest/Code/jart/cosmopolitan
CC         = $(COSMO)/bin/cosmocc
ZIPCOPY    = $(COSMO)/bin/zipcopy
CFLAGS     = -Wall -Wextra -Werror -O2 -std=c17 -D_COSMO_SOURCE -I$(COSMO_SRC) -I../../vendor
BUILDDIR   = ../../build

# All objects except main.o and wowx_main.o for linking demos
ALL_OBJS = $(filter-out $(BUILDDIR)/main.o $(BUILDDIR)/wowx_main.o,$(wildcard $(BUILDDIR)/*.o) \
           $(wildcard $(BUILDDIR)/http/*.o) \
           $(wildcard $(BUILDDIR)/download/*.o) \
           $(wildcard $(BUILDDIR)/rubies/*.o) \
           $(wildcard $(BUILDDIR)/gems/*.o) \
           $(wildcard $(BUILDDIR)/gemfile/*.o) \
           $(wildcard $(BUILDDIR)/pubgrub/*.o) \
           $(wildcard $(BUILDDIR)/resolver/*.o) \
           $(wildcard $(BUILDDIR)/util/*.o))
TLS_LIB    = $(BUILDDIR)/libtls.a
YAML_LIB   = $(BUILDDIR)/libyaml.a
ASSETS_ZIP = $(BUILDDIR)/assets.zip

DEMOS = demo_sync.com demo_run.com

all: $(DEMOS)

demo_sync.com: demo_sync.c $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB) $(ASSETS_ZIP)
	$(CC) $(CFLAGS) -I../../include -o $@ $< $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

demo_run.com: demo_run.c $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB) $(ASSETS_ZIP)
	$(CC) $(CFLAGS) -I../../include -o $@ $< $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

clean:
	rm -f $(DEMOS) *.com.dbg *.aarch64.elf

.PHONY: all clean
