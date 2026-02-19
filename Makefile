# Include config.mk from ./configure if it exists
-include config.mk

# Fallbacks if config.mk not present (or vars not set)
COSMO     ?= /home/groobiest/Code/jart/cosmopolitan/.cosmocc/3.9.2
COSMO_SRC ?= /home/groobiest/Code/jart/cosmopolitan

CC        = $(COSMO)/bin/cosmocc
AR        = $(COSMO)/bin/cosmoar
ZIPCOPY   = $(COSMO)/bin/zipcopy
CFLAGS    = -Wall -Wextra -Werror -O2 -std=c17 -D_COSMO_SOURCE -I$(COSMO_SRC) -MMD -MP
BUILDDIR  = build

# Cosmo source uses -std=gnu23; we use gnu11 + stdbool to match
# Match cosmo's build/definitions.mk: -Wa,-W suppresses assembler warnings
# (ecp256/ecp384 inline asm uses offset+memory-operand syntax that warns
# but generates correct code). -Wa,--noexecstack marks stack non-executable.
COSMO_CFLAGS = -O2 -std=gnu17 -D_COSMO_SOURCE -I$(COSMO_SRC) \
               -fdata-sections -ffunction-sections -include stdbool.h \
               -Werror -Wno-prio-ctor-dtor \
               -Wa,-W -Wa,--noexecstack

# --- wow sources ---
SRCS = $(wildcard src/*.c) \
       $(wildcard src/http/*.c) \
       $(wildcard src/download/*.c) \
       $(wildcard src/rubies/*.c) \
       $(wildcard src/gems/*.c) \
       $(wildcard src/gemfile/*.c) \
       $(wildcard src/util/*.c)
OBJS = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(SRCS)) \
       $(BUILDDIR)/cJSON.o

# Auto-generated header dependencies (GCC -MMD -MP writes .d files alongside .o)
-include $(OBJS:.o=.d)

# Create subdirectories for object files
OBJDIRS = $(BUILDDIR)/http $(BUILDDIR)/download $(BUILDDIR)/rubies $(BUILDDIR)/gems $(BUILDDIR)/gemfile $(BUILDDIR)/util $(BUILDDIR)/internal

# --- mbedTLS + HTTPS from cosmo source (compiled with cosmocc) ---
MBEDTLS_SRCS = $(wildcard $(COSMO_SRC)/third_party/mbedtls/*.c)
HTTPS_SRCS   = $(wildcard $(COSMO_SRC)/net/https/*.c)
MBEDTLS_OBJS = $(patsubst $(COSMO_SRC)/third_party/mbedtls/%.c,$(BUILDDIR)/mbedtls/%.o,$(MBEDTLS_SRCS))
HTTPS_OBJS_ALL = $(patsubst $(COSMO_SRC)/net/https/%.c,$(BUILDDIR)/https/%.o,$(HTTPS_SRCS))
# Exclude getentropy.o — it calls arc4random_buf which isn't in cosmocc's libcosmo.a.
# Our src/entropy.c provides GetEntropy() using getentropy(2) instead.
HTTPS_OBJS   = $(filter-out $(BUILDDIR)/https/getentropy.o,$(HTTPS_OBJS_ALL))
TLS_LIB      = $(BUILDDIR)/libtls.a

# --- libyaml from cosmo source (compiled with cosmocc) ---
LIBYAML_SRCS = $(wildcard $(COSMO_SRC)/third_party/libyaml/*.c)
LIBYAML_OBJS = $(patsubst $(COSMO_SRC)/third_party/libyaml/%.c,$(BUILDDIR)/libyaml/%.o,$(LIBYAML_SRCS))
LIBYAML_CFLAGS = $(COSMO_CFLAGS) -I$(COSMO_SRC)/third_party/libyaml \
                 -DYAML_DECLARE_STATIC -include $(COSMO_SRC)/third_party/libyaml/config.h \
                 -Wno-unused-value
LIBYAML_LIB  = $(BUILDDIR)/libyaml.a

# --- APE zip assets (single zip — zipcopy only supports one call per binary) ---
SSL_ROOTS        = $(wildcard $(COSMO_SRC)/usr/share/ssl/root/*.pem)
RUBY_BINARY_DEFS = vendor/ruby-binary/share/ruby-binary/repos
ASSETS_ZIP       = $(BUILDDIR)/assets.zip

# --- build targets ---
# NB: wow.com MUST be the first target in the Makefile — GNU Make uses the
# first target as the default goal when you run bare `make`.
$(BUILDDIR)/wow.com: $(OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

$(ASSETS_ZIP): $(SSL_ROOTS) $(wildcard $(RUBY_BINARY_DEFS)/ruby-builder/*) $(wildcard $(RUBY_BINARY_DEFS)/cosmoruby/*) | $(BUILDDIR)
	cd $(COSMO_SRC) && zip -q $(CURDIR)/$@ usr/share/ssl/root/*.pem
	cd $(RUBY_BINARY_DEFS) && zip -q $(CURDIR)/$@ ruby-builder/* cosmoruby/* 2>/dev/null || \
	cd $(RUBY_BINARY_DEFS) && zip -q $(CURDIR)/$@ ruby-builder/*

# Pattern rules for source subdirectories
$(BUILDDIR)/%.o: src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -c $< -o $@

$(BUILDDIR)/http/%.o: src/http/%.c | $(BUILDDIR)/http
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -c $< -o $@

$(BUILDDIR)/download/%.o: src/download/%.c | $(BUILDDIR)/download
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -c $< -o $@

$(BUILDDIR)/rubies/%.o: src/rubies/%.c | $(BUILDDIR)/rubies
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -c $< -o $@

$(BUILDDIR)/gems/%.o: src/gems/%.c | $(BUILDDIR)/gems
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -I$(COSMO_SRC)/third_party/libyaml -c $< -o $@

$(BUILDDIR)/gemfile/%.o: src/gemfile/%.c | $(BUILDDIR)/gemfile
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -Isrc/gemfile -c $< -o $@

# parser.c is lemon-generated — suppress GCC false positives on table bounds
$(BUILDDIR)/gemfile/parser.o: src/gemfile/parser.c | $(BUILDDIR)/gemfile
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -Isrc/gemfile \
		-Wno-array-bounds -Wno-maybe-uninitialized -c $< -o $@

$(BUILDDIR)/util/%.o: src/util/%.c | $(BUILDDIR)/util
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -Wno-unused-parameter -c $< -o $@

$(BUILDDIR)/cJSON.o: vendor/cjson/cJSON.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -Ivendor/cjson -c $< -o $@

# entropy.c is a cosmo shim — needs COSMO_CFLAGS for stdbool/gnu11
$(BUILDDIR)/http/entropy.o: src/http/entropy.c | $(BUILDDIR)/http
	$(CC) $(COSMO_CFLAGS) -c $< -o $@

$(BUILDDIR)/mbedtls/%.o: $(COSMO_SRC)/third_party/mbedtls/%.c | $(BUILDDIR)/mbedtls
	$(CC) $(COSMO_CFLAGS) -c $< -o $@

$(BUILDDIR)/https/%.o: $(COSMO_SRC)/net/https/%.c | $(BUILDDIR)/https
	$(CC) $(COSMO_CFLAGS) -c $< -o $@

$(TLS_LIB): $(MBEDTLS_OBJS) $(HTTPS_OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/libyaml/%.o: $(COSMO_SRC)/third_party/libyaml/%.c | $(BUILDDIR)/libyaml
	$(CC) $(LIBYAML_CFLAGS) -c $< -o $@

$(LIBYAML_LIB): $(LIBYAML_OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/http: | $(BUILDDIR)
	mkdir -p $@

$(BUILDDIR)/download: | $(BUILDDIR)
	mkdir -p $@

$(BUILDDIR)/rubies: | $(BUILDDIR)
	mkdir -p $@

$(BUILDDIR)/gems: | $(BUILDDIR)
	mkdir -p $@

$(BUILDDIR)/gemfile: | $(BUILDDIR)
	mkdir -p $@

$(BUILDDIR)/util: | $(BUILDDIR)
	mkdir -p $@

$(BUILDDIR)/mbedtls: | $(BUILDDIR)
	mkdir -p $@

$(BUILDDIR)/https: | $(BUILDDIR)
	mkdir -p $@

$(BUILDDIR)/libyaml: | $(BUILDDIR)
	mkdir -p $@

# --- Tests ---
TEST_HTTP_OBJS = $(filter-out $(BUILDDIR)/main.o,$(OBJS))

$(BUILDDIR)/tls_test.com: tests/tls_test.c $(TEST_HTTP_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP) | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -o $@ $< $(TEST_HTTP_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

$(BUILDDIR)/registry_test.com: tests/registry_test.c $(TEST_HTTP_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP) | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -o $@ $< $(TEST_HTTP_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

$(BUILDDIR)/ruby_mgr_test.com: tests/ruby_mgr_test.c $(TEST_HTTP_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP) | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -o $@ $< $(TEST_HTTP_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

test-tls: $(BUILDDIR)/tls_test.com
	$(BUILDDIR)/tls_test.com

test-registry: $(BUILDDIR)/registry_test.com
	$(BUILDDIR)/registry_test.com

test-ruby-mgr: $(BUILDDIR)/ruby_mgr_test.com
	$(BUILDDIR)/ruby_mgr_test.com

$(BUILDDIR)/gem_test.com: tests/gem_test.c $(TEST_HTTP_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP) | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -I$(COSMO_SRC)/third_party/libyaml -o $@ $< $(TEST_HTTP_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

test-gem: $(BUILDDIR)/gem_test.com
	$(BUILDDIR)/gem_test.com

$(BUILDDIR)/gemfile_test.com: tests/gemfile_test.c $(TEST_HTTP_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP) | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -Isrc/gemfile -o $@ $< $(TEST_HTTP_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

test-gemfile: $(BUILDDIR)/gemfile_test.com
	$(BUILDDIR)/gemfile_test.com

test: test-tls test-registry test-ruby-mgr test-gem test-gemfile

# --- Code generation (developer-only, outputs committed) ---
generate-gemfile-parser:
	lemon src/gemfile/parser.y
	re2c -o src/gemfile/lexer.c src/gemfile/lexer.re --no-debug-info

# --- Corpus pipeline ---
.PHONY: corpus-seed corpus-enrich corpus-download corpus-test corpus-report

corpus-seed:
	cd lode && bash seed.sh

corpus-enrich:
	cd lode && bash enrich.sh

corpus-download:
	cd lode && bash download.sh

corpus-test: $(BUILDDIR)/wow.com
	cd lode && WOW=$(CURDIR)/$(BUILDDIR)/wow.com bash test.sh

corpus-report:
	cd lode && bash report.sh

clean:
	rm -rf $(BUILDDIR)

# fresh: remove wow's own build artefacts but preserve 3rd-party libraries
# (libtls.a, libyaml.a, assets.zip) to avoid recompiling cosmo sources
fresh:
	@echo "Removing wow build artefacts (preserving 3rd-party libs)..."
	rm -f $(OBJS) $(OBJS:.o=.d)
	rm -f $(TEST_BINS) $(TEST_BINS:.com=.com.dbg)
	rm -f $(BUILDDIR)/wow.com $(BUILDDIR)/wow.com.dbg
	rm -f $(patsubst $(BUILDDIR)/%, $(BUILDDIR)/.aarch64/%, $(OBJS))
	rm -f $(patsubst $(BUILDDIR)/%, $(BUILDDIR)/.aarch64/%, $(OBJS:.o=.d))
	@echo "Preserved: libtls.a, libyaml.a, assets.zip"

distclean: clean
	rm -f config.mk

.PHONY: clean fresh distclean test test-tls test-registry test-ruby-mgr test-gem test-gemfile generate-gemfile-parser
