# Phase 1: Skeleton + Build System

> Get a Cosmopolitan C binary building and dispatching subcommands.

## 1a: Hello World with cosmocc

**Demo:** `./build/wow.com` prints "wow"

**Files:**
- `Makefile`
- `src/main.c`

**Makefile sketch:**
```makefile
COSMO    = /home/groobiest/Code/jart/cosmopolitan
CC       = $(COSMO)/bin/cosmocc
CFLAGS   = -O2 -Wall -Wextra
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
```

**main.c sketch:**
```c
#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("wow\n");
    return 0;
}
```

**Verify:** `make && ./build/wow.com` → prints "wow"

## 1b: Subcommand Dispatch

**Demo:** `./wow.com --help` shows available commands, `./wow.com init` prints a stub.

**Changes to main.c:**
- Parse argv[1] as subcommand
- Dispatch table: `init`, `sync`, `lock`, `add`, `remove`, `run`, `ruby`, `bundle`
- `--help` / `-h` prints usage
- `--version` prints version
- Unknown subcommand → error + usage

**Verify:**
- `./build/wow.com --help` → shows command list
- `./build/wow.com init` → "wow init: not yet implemented"
- `./build/wow.com bogus` → "unknown command: bogus"

## 1c: wow init

**Demo:** `./wow.com init greg` creates a project directory with Gemfile and .ruby-version.

**Files:**
- `src/init.c`
- `include/wow/init.h`

**Behaviour:**
- `wow init greg` → creates `greg/` directory
- `wow init` (no arg) → initialises current directory
- Writes `Gemfile`:
  ```ruby
  # frozen_string_literal: true

  source "https://rubygems.org"

  # gem "rails"
  ```
- Writes `.ruby-version`: `4.0` (latest)
- Ruby download is stubbed for now (Phase 3)
- Error if Gemfile already exists (unless `--force`)

**Verify:**
```bash
./build/wow.com init testproject
cat testproject/Gemfile
cat testproject/.ruby-version
```
