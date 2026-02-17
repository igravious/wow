# Phase 5 demos — Gemfile lexer + parser (link against main project build)
-include ../../config.mk

COSMO     ?= /home/groobiest/Code/jart/cosmopolitan/.cosmocc/3.9.2
COSMO_SRC ?= /home/groobiest/Code/jart/cosmopolitan
CC         = $(COSMO)/bin/cosmocc
ZIPCOPY    = $(COSMO)/bin/zipcopy
CFLAGS     = -Wall -Wextra -Werror -O2 -std=c17 -D_COSMO_SOURCE -I$(COSMO_SRC)
BUILDDIR   = ../../build

# Objects from the main build needed by Phase 5 demos
# (the gemfile module + everything it transitively depends on)
GEMFILE_OBJS = $(BUILDDIR)/gemfile/cmd.o $(BUILDDIR)/gemfile/glue.o \
               $(BUILDDIR)/gemfile/lexer.o $(BUILDDIR)/gemfile/parser.o \
               $(BUILDDIR)/gemfile/types.o

# Phase 5 demos are pure parsing — no network needed, but the linker
# pulls in symbols from other modules transitively. Use TEST_HTTP_OBJS
# pattern: all objects minus main.o.
ALL_OBJS = $(filter-out $(BUILDDIR)/main.o,$(wildcard $(BUILDDIR)/*.o) \
           $(wildcard $(BUILDDIR)/http/*.o) \
           $(wildcard $(BUILDDIR)/download/*.o) \
           $(wildcard $(BUILDDIR)/rubies/*.o) \
           $(wildcard $(BUILDDIR)/gems/*.o) \
           $(wildcard $(BUILDDIR)/gemfile/*.o) \
           $(wildcard $(BUILDDIR)/util/*.o))
TLS_LIB    = $(BUILDDIR)/libtls.a
YAML_LIB   = $(BUILDDIR)/libyaml.a
SSL_ZIP    = $(BUILDDIR)/ssl-roots.zip

DEMOS = demo_lex.com demo_parse.com demo_roundtrip.com

all: $(DEMOS)

demo_lex.com: demo_lex.c $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB) $(SSL_ZIP)
	$(CC) $(CFLAGS) -I../../include -I../../src/gemfile -o $@ $< $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB)
	$(ZIPCOPY) $(SSL_ZIP) $@

demo_parse.com: demo_parse.c $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB) $(SSL_ZIP)
	$(CC) $(CFLAGS) -I../../include -I../../src/gemfile -o $@ $< $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB)
	$(ZIPCOPY) $(SSL_ZIP) $@

demo_roundtrip.com: demo_roundtrip.c $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB) $(SSL_ZIP)
	$(CC) $(CFLAGS) -I../../include -I../../src/gemfile -o $@ $< $(ALL_OBJS) $(TLS_LIB) $(YAML_LIB)
	$(ZIPCOPY) $(SSL_ZIP) $@

clean:
	rm -f $(DEMOS) *.com.dbg *.aarch64.elf

.PHONY: all clean
