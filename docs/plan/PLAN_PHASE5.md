# Phase 5: Gemfile Parser

> Parse Gemfiles without a Ruby interpreter, using re2c + lemon.

## 5a: re2c Lexer — Tokenise a Gemfile

**Demo:** `./wow.com debug gemfile-lex Gemfile` prints token stream.

**Files:**
- `src/gemfile.l.re2c` (re2c input)
- `src/gemfile_lexer.c` (generated)

**Tokens:**
```
TOKEN_SOURCE     "source"
TOKEN_GEM        "gem"
TOKEN_GROUP      "group"
TOKEN_DO         "do"
TOKEN_END        "end"
TOKEN_RUBY       "ruby"
TOKEN_GEMSPEC    "gemspec"
TOKEN_STRING     "..." or '...'
TOKEN_SYMBOL     :name
TOKEN_COMMA      ,
TOKEN_COLON      :
TOKEN_HASHROCKET =>
TOKEN_NEWLINE    \n
TOKEN_COMMENT    # ...
TOKEN_EOF
TOKEN_ERROR      (anything unexpected → clean error with line number)
```

**Implementation:**
- re2c input file with token rules
- Generate C lexer: `re2c -o src/gemfile_lexer.c src/gemfile.l.re2c`
- Add generation rule to Makefile
- Lexer returns token type + value + line number

**Verify:**
```bash
./build/wow.com debug gemfile-lex Gemfile
# 1: TOKEN_COMMENT   "# frozen_string_literal: true"
# 3: TOKEN_SOURCE    "source"
# 3: TOKEN_STRING    "https://rubygems.org"
# 5: TOKEN_GEM       "gem"
# 5: TOKEN_STRING    "sinatra"
# 5: TOKEN_COMMA     ","
# 5: TOKEN_STRING    "~> 4.0"
```

## 5b: lemon Parser — Parse Into Structure

**Demo:** `./wow.com gemfile-parse Gemfile` prints parsed dependency structure.

**Files:**
- `src/gemfile.y` (lemon grammar)
- `src/gemfile_parser.c` (generated)
- `src/gemfile.c` (glue)
- `include/wow/gemfile.h`

**Grammar (simplified):**
```
file       ::= statements.
statements ::= statements statement.
statements ::= .
statement  ::= source_stmt | gem_stmt | group_stmt | ruby_stmt | gemspec_stmt.
source_stmt ::= SOURCE STRING.
gem_stmt    ::= GEM STRING gem_opts.
gem_opts    ::= gem_opts COMMA STRING.       /* version constraints */
gem_opts    ::= gem_opts COMMA key_value.    /* options like group:, require: */
gem_opts    ::= .
group_stmt  ::= GROUP symbols DO statements END.
ruby_stmt   ::= RUBY STRING.
gemspec_stmt ::= GEMSPEC.
```

**Implementation:**
- lemon grammar file
- Generate parser: `lemon src/gemfile.y`
- Parser builds a dependency list struct:
  ```c
  typedef struct {
      char *name;
      char **constraints;   /* "~> 4.0", ">= 1.0" */
      int  n_constraints;
      char *group;          /* "development", "test", or NULL */
      bool require;         /* false if require: false */
  } wow_gem_dep;
  ```
- Clean error messages with line numbers for unsupported syntax

**Verify:**
```bash
./build/wow.com gemfile-parse Gemfile
# source: https://rubygems.org
# gem: sinatra ~> 4.0
# gem: rack >= 3.0.0, < 4
# group: development
#   gem: pry (require: false)
```

## 5c: Extract Dependency List

**Demo:** `./wow.com gemfile-deps Gemfile` prints just the dependency names + constraints.

**Implementation:**
- Wire lexer → parser → dependency list
- Output machine-readable format (one gem per line)
- This is the entry point for the resolver (Phase 6)

**Verify:**
```bash
./build/wow.com gemfile-deps Gemfile
# sinatra ~> 4.0
# rack >= 3.0.0, < 4
# pry (group: development)
```

## Build System Integration

Add to Makefile:
```makefile
src/gemfile_lexer.c: src/gemfile.l.re2c
	re2c -o $@ $<

src/gemfile_parser.c: src/gemfile.y
	lemon $<
```

Ensure re2c and lemon are available as build-time dependencies.
