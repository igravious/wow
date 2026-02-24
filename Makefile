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

# Cosmo source uses -std=gnu23; we use gnu17 + stdbool to bridge
# bool/true/false which are keywords in gnu23 but not gnu17.
# Match cosmo's build/definitions.mk: -Wa,-W suppresses assembler warnings
# (ecp256/ecp384 inline asm uses offset+memory-operand syntax that warns
# but generates correct code). -Wa,--noexecstack marks stack non-executable.
#
# -Wno-prio-ctor-dtor: GCC warns when __attribute__((constructor(priority)))
# uses priorities 0–100, which are reserved for the C runtime implementation.
# Cosmopolitan IS a C runtime — it uses reserved-range priorities to bootstrap
# its portable runtime (OS detection, syscall layer, memory mappings, TLS)
# before any normal constructors or user code run. The warning is a false
# positive: it exists to stop application code stomping on runtime init order,
# but cosmo is the runtime. We cannot fix these — they are in cosmo's source.
COSMO_CFLAGS = -O2 -std=gnu17 -D_COSMO_SOURCE -I$(COSMO_SRC) \
               -fdata-sections -ffunction-sections -include stdbool.h \
               -Werror -Wno-prio-ctor-dtor \
               -Wa,-W -Wa,--noexecstack

# --- wow sources (exclude wowx_main.c — it has its own entry point) ---
SRCS_ALL = $(wildcard src/*.c) \
           $(wildcard src/http/*.c) \
           $(wildcard src/download/*.c) \
           $(wildcard src/rubies/*.c) \
           $(wildcard src/gems/*.c) \
           $(wildcard src/gemfile/*.c) \
           $(wildcard src/resolver/*.c) \
           $(wildcard src/exec/*.c) \
           $(wildcard src/util/*.c)
SRCS = $(filter-out src/wowx_main.c,$(SRCS_ALL))
OBJS = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(SRCS)) \
       $(BUILDDIR)/cJSON.o

# Auto-generated header dependencies (GCC -MMD -MP writes .d files alongside .o)
-include $(OBJS:.o=.d)
-include $(BUILDDIR)/wowx_main.d

# Resolver debug test harness (source lives under tests/ but links into wow.com)
RESOLVER_TEST_SRCS = $(wildcard tests/resolver/*.c)
RESOLVER_TEST_OBJS = $(patsubst tests/resolver/%.c,$(BUILDDIR)/resolver/test/%.o,$(RESOLVER_TEST_SRCS))
-include $(RESOLVER_TEST_OBJS:.o=.d)

# Create subdirectories for object files
OBJDIRS = $(BUILDDIR)/http $(BUILDDIR)/download $(BUILDDIR)/rubies $(BUILDDIR)/gems $(BUILDDIR)/gemfile $(BUILDDIR)/resolver $(BUILDDIR)/resolver/test $(BUILDDIR)/exec $(BUILDDIR)/util $(BUILDDIR)/internal

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
# -Wno-unused-value: libyaml's macros expand to compound expressions (comma
# operator, (void)expr casts) where intermediate values are intentionally
# discarded. GCC flags these as unused. Third-party source we don't own,
# compiled with -Werror, so we suppress the false positive.
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

# Shared objects (everything except wow's main.o) — used by wowx + tests
SHARED_OBJS = $(filter-out $(BUILDDIR)/main.o,$(OBJS))

# --- build targets ---
all: $(BUILDDIR)/wow.com $(BUILDDIR)/wowx.com

$(BUILDDIR)/wow.com: $(OBJS) $(RESOLVER_TEST_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(RESOLVER_TEST_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

$(BUILDDIR)/wowx.com: $(BUILDDIR)/wowx_main.o $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP)
	$(CC) $(CFLAGS) -o $@ $(BUILDDIR)/wowx_main.o $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
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

# parser.c is generated by Lemon (SQLite's LALR(1) parser generator). Lemon
# emits action/goto lookup tables indexed by parser state. GCC's static analysis
# cannot reason through the state machine to prove indices are in-bounds, so it
# falsely reports -Warray-bounds on table accesses. Similarly, variables set
# inside Lemon's reduce-action switch (which is exhaustive over valid states) are
# flagged as -Wmaybe-uninitialized because GCC cannot prove all paths through
# the generated code assign them. Both are false positives — the parser state
# machine guarantees valid indices and complete initialisation. Suppression is
# scoped to this single generated file only, not to any hand-written code.
$(BUILDDIR)/gemfile/parser.o: src/gemfile/parser.c | $(BUILDDIR)/gemfile
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -Isrc/gemfile \
		-Wno-array-bounds -Wno-maybe-uninitialized -c $< -o $@

$(BUILDDIR)/resolver/%.o: src/resolver/%.c | $(BUILDDIR)/resolver
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -c $< -o $@

$(BUILDDIR)/resolver/test/%.o: tests/resolver/%.c | $(BUILDDIR)/resolver/test
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -Itests/resolver -c $< -o $@

$(BUILDDIR)/exec/%.o: src/exec/%.c | $(BUILDDIR)/exec
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -c $< -o $@

$(BUILDDIR)/util/%.o: src/util/%.c | $(BUILDDIR)/util
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -c $< -o $@

# sha256.c includes mbedTLS headers, which pull in cosmo's runtime.h. That
# header has a forceinline __trace_disabled(int x) with an unused parameter.
# Cosmo's own code we don't own — suppress for this single file only.
$(BUILDDIR)/util/sha256.o: src/util/sha256.c | $(BUILDDIR)/util
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

$(BUILDDIR)/resolver: | $(BUILDDIR)
	mkdir -p $@

$(BUILDDIR)/resolver/test: | $(BUILDDIR)/resolver
	mkdir -p $@

$(BUILDDIR)/exec: | $(BUILDDIR)
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
TEST_BINS = $(BUILDDIR)/tls_test.com $(BUILDDIR)/registry_test.com \
            $(BUILDDIR)/ruby_mgr_test.com $(BUILDDIR)/gem_test.com \
            $(BUILDDIR)/gemfile_test.com $(BUILDDIR)/resolver_test.com \
            $(BUILDDIR)/arena_offset_test.com

$(BUILDDIR)/tls_test.com: tests/tls_test.c $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP) | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -o $@ $< $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

$(BUILDDIR)/registry_test.com: tests/registry_test.c $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP) | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -o $@ $< $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

$(BUILDDIR)/ruby_mgr_test.com: tests/ruby_mgr_test.c $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP) | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -o $@ $< $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

test-tls: $(BUILDDIR)/tls_test.com
	$(BUILDDIR)/tls_test.com

test-registry: $(BUILDDIR)/registry_test.com
	$(BUILDDIR)/registry_test.com

test-ruby-mgr: $(BUILDDIR)/ruby_mgr_test.com
	$(BUILDDIR)/ruby_mgr_test.com

$(BUILDDIR)/gem_test.com: tests/gem_test.c $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP) | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -I$(COSMO_SRC)/third_party/libyaml -o $@ $< $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

test-gem: $(BUILDDIR)/gem_test.com
	$(BUILDDIR)/gem_test.com

$(BUILDDIR)/gemfile_test.com: tests/gemfile_test.c $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP) | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -Isrc/gemfile -o $@ $< $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

test-gemfile: $(BUILDDIR)/gemfile_test.com
	$(BUILDDIR)/gemfile_test.com

$(BUILDDIR)/resolver_test.com: tests/resolver_test.c $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP) | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -o $@ $< $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

test-resolver: $(BUILDDIR)/resolver_test.com
	$(BUILDDIR)/resolver_test.com

$(BUILDDIR)/arena_offset_test.com: tests/arena_offset_test.c $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB) $(ASSETS_ZIP) | $(BUILDDIR)
	$(CC) $(CFLAGS) -Iinclude -Ivendor/cjson -o $@ $< $(SHARED_OBJS) $(TLS_LIB) $(LIBYAML_LIB)
	$(ZIPCOPY) $(ASSETS_ZIP) $@

test-arena-offset: $(BUILDDIR)/arena_offset_test.com
	$(BUILDDIR)/arena_offset_test.com

test: test-tls test-registry test-ruby-mgr test-gem test-gemfile test-resolver test-arena-offset

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
	rm -f $(BUILDDIR)/wowx_main.o $(BUILDDIR)/wowx_main.d
	rm -f $(BUILDDIR)/wowx.com $(BUILDDIR)/wowx.com.dbg
	rm -f $(patsubst $(BUILDDIR)/%, $(BUILDDIR)/.aarch64/%, $(OBJS))
	rm -f $(patsubst $(BUILDDIR)/%, $(BUILDDIR)/.aarch64/%, $(OBJS:.o=.d))
	@echo "Preserved: libtls.a, libyaml.a, assets.zip"

distclean: clean
	rm -f config.mk

.PHONY: all clean fresh distclean test test-tls test-registry test-ruby-mgr test-gem test-gemfile test-resolver test-arena-offset generate-gemfile-parser
