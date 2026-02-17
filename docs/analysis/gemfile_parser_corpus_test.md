# Gemfile Parser Corpus Test — 17 February 2026

## Summary

Tested wow's re2c/lemon Gemfile parser (`src/gemfile/`) against 2,812 real-world
Gemfiles harvested from RubyGems → GitHub (see `snarf/README.md` for the
collection pipeline).

| | Count | % |
|---|---:|---:|
| **Pass** | 2,110 | 75.0% |
| **Fail** | 702 | 25.0% |
| **Total** | 2,812 | |

**Target: 100% fidelity** on every construct we encounter. Every source, every
dependency, every constraint — we need a handle on all of it.

**Critical finding:** Pass rate is *inversely correlated* with gem popularity.
The top 100 gems by downloads have only a 55% pass rate. The most-downloaded
gem in our corpus (addressable, 1.1B downloads) fails.

---

## Download-Weighted Analysis

The overall 75% pass rate masks a worse picture for high-download gems.
Popular gems tend to have more complex Gemfiles with conditional logic,
platform blocks, git sources, etc.

### Pass rates by download tier

| Tier | Pass | Fail | Pass % |
|---|---:|---:|---:|
| Top 10 | 6 | 4 | 60.0% |
| Top 25 | 10 | 15 | 40.0% |
| Top 50 | 27 | 23 | 54.0% |
| Top 100 | 55 | 45 | 55.0% |
| Top 250 | 150 | 100 | 60.0% |
| Top 500 | 328 | 172 | 65.6% |
| All 2,812 | 2,110 | 702 | 75.0% |

**The most popular gems are the hardest to parse.** The top 100 by downloads
have a 55% pass rate vs. 75% overall.

### Top 10 gems by downloads

| Rank | Gem | Downloads | Status | Root cause |
|---:|---|---:|---|---|
| 1 | addressable | 1,118M | **FAIL** | trailing `if` on gem line |
| 2 | activerecord-import | 148M | **FAIL** | Ruby code (`version = ENV[...].to_f`) |
| 3 | aes_key_wrap | 93M | PASS | |
| 4 | aasm | 93M | PASS | |
| 5 | afm | 86M | PASS | |
| 6 | activemodel-serializers-xml | 85M | PASS | |
| 7 | acts-as-taggable-on | 65M | **FAIL** | `platforms: [:mri]` — array literal |
| 8 | acts_as_list | 64M | **FAIL** | Ruby code (`rails_version = Gem::Version.new(...)`) |
| 9 | airbrake | 48M | PASS | |
| 10 | activerecord-session_store | 47M | PASS | |

### Root causes across top 100 failures (45 failures)

| Root cause | Count | Example |
|---|---:|---|
| **Trailing `if`/`unless`** | 14 | `gem "byebug" if ENV["BYEBUG"]` |
| **`git_source`** | 9 | `git_source(:github) { \|r\| "https://..." }` |
| **Ruby code** (assignments, `require`, `def`) | 8 | `rails_version = ENV["RAILS_VERSION"]` |
| **`eval_gemfile`** | 7 | `eval_gemfile "gemfiles/common.gemfile"` |
| **`platforms:` array** | 3 | `gem "byebug", platforms: [:mri]` |
| **`gemspec` missing comma** | 2 | `gemspec path: "."` (no leading comma) |
| **`platforms` block** | 1 | `platforms :jruby do ... end` |
| **`gemspec` hashrocket** | 1 | `gemspec :development_group => :test` |

---

## Methodology

### Corpus

2,812 Gemfiles in `snarf/data/gemfiles/*.Gemfile`, one per gem, scraped from
GitHub default branches of gems listed on RubyGems.org. The corpus covers the
full spectrum from trivial (`source + gemspec`) to complex (conditional
platform blocks, `eval_gemfile`, `ENV[]`-driven version switching).

Largest file: 114 lines (`activerecord-jdbc-alt-adapter.Gemfile`). Median is
around 5–10 lines. 28,717 total lines across all files.

### Test procedure

```bash
pass=0; fail=0
for f in snarf/data/gemfiles/*.Gemfile; do
    if ./build/wow.com gemfile-parse "$f" >/dev/null 2>/tmp/wow-gemfile-test/$(basename "$f").err; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
    fi
done
```

Each Gemfile was fed to `wow gemfile-parse`. Exit code 0 = pass, non-zero =
fail. Stderr was captured per-file for post-hoc categorisation.

### Categorisation

Failures were binned by the first line of stderr output:

- **`unsupported syntax`** — lexer emitted an `UNSUPPORTED` token (construct
  deliberately flagged: `if`, `unless`, `case`, `eval`, `eval_gemfile`,
  `git_source`)
- **`syntax error`** — tokens lexed fine but the lemon grammar rejected the
  sequence
- **`unexpected character`** — lexer emitted an `ERROR` token (byte not
  matched by any re2c rule)

Within each bin, the offending source line (from stderr line 2) was extracted
and further grouped by construct pattern using `sort | uniq -c | sort -rn`.

---

## Failure Breakdown

### By error category

| Category | Count | % of failures | % of total |
|---|---:|---:|---:|
| **Unsupported token** (lexer flags known-bad construct) | 439 | 62.5% | 15.6% |
| **Syntax error** (parser rejects valid tokens) | 222 | 31.6% | 7.9% |
| **Unexpected character** (lexer can't tokenise) | 41 | 5.8% | 1.5% |

---

### UNSUPPORTED — 439 failures

The lexer explicitly returns `UNSUPPORTED` for these keywords, causing
immediate abort.

| Construct | Count | What it does |
|---|---:|---|
| `git_source(:github) { \|repo\| ... }` | 335 | Defines URL template for `github:` gem key |
| `if` (standalone block) | 49 | Conditional sections: `if ENV['RAILS']` |
| `gem "x" if/unless ...` | 26 | Trailing conditional on gem line |
| `eval_gemfile "other.gemfile"` | 15 | Include another Gemfile |
| `case ENV["ADAPTER"]` | 5 | Multi-branch conditional |
| `unless` | 3 | Negative conditional |
| `eval(...)` | 2 | Raw Ruby eval |
| Other variants | 4 | `git_source :github do`, etc. |

#### `git_source` is NOT ignorable

`git_source(:github)` defines the URL template that `github:` keys in gem
declarations resolve against. 65 files in the corpus use `github:` as a gem
option (e.g. `gem "rails", github: "rails/rails", branch: "main"`). Without
parsing `git_source`, we don't know *where* those gems come from.

In practice, the template is nearly always one of:
```ruby
git_source(:github) { |repo| "https://github.com/#{repo}.git" }
git_source(:github) { |repo| "https://github.com/#{repo}" }
```

We can hard-code the `github` source to `https://github.com/{repo}[.git]` as a
well-known default and still parse the `git_source` line for completeness.

---

### SYNTAX ERROR — 222 failures (parser rejects)

Tokens lex fine but the grammar has no rule to accept them.

| Pattern | Count | Description | Fix |
|---|---:|---|---|
| `source :rubygems` / `:gemcutter` | 58 | Legacy symbol-style source | Grammar: allow SYMBOL after SOURCE |
| Arbitrary Ruby code | 71 | `require`, `def`, variable assignments, `File.*`, `Dir.*` — top-level Ruby | Won't parse (need Ruby eval) |
| `platforms :ruby do ... end` | 24 | Platform-conditional block | Grammar: new `platforms_stmt` rule |
| `require: nil` | 19 | Keyword arg with nil value | Grammar: accept `nil` literal |
| `source "url" do ... end` | 11 | Scoped source block | Grammar: new source block rule |
| `gemspec :key => val` (hashrocket) | 7 | Hashrocket opts on gemspec | Grammar: add to `gemspec_opts` |
| `gemspec path: "subdir"` already handled, but `gemspec name: "x"` | 6 | Named gemspec | Grammar: ensure `name:` key works |
| `ruby RUBY_VERSION` / `ruby file: ".ruby-version"` | 4 | Dynamic ruby version | Grammar: `file:` key + IDENT fallback |
| `group 'test' do` (string name) | 3 | String instead of symbol for group | Grammar: allow STRING in group |
| `gemspec name: "x"` | 3 | Named gemspec selection | Grammar: add to `gemspec_opts` |
| `plugin "bundler-multilock"` | 2 | Bundler plugin directive | Grammar: new `plugin_stmt` rule |
| Other Ruby code | 14 | Ternary operators, `ENV.fetch`, `module`, etc. | Won't parse |

---

### UNEXPECTED CHARACTER — 41 failures (lexer rejects)

The lexer has no rule for these bytes.

| Pattern | Count | Missing token |
|---|---:|---|
| `[:ruby, :jruby]` — array literals | 34 | `[` and `]` |
| `source("url")` / `group(:dev)` — paren-style calls | 6 | `(` and `)` |
| `=begin` block comment | 1 | `=begin...=end` block |

---

## Construct Prevalence (all 2,812 files)

How common is each construct across the entire corpus, not just failures?

| Construct | Files using it | % of corpus |
|---|---:|---:|
| `git_source` | 338 | 12.0% |
| Array literals `[:sym, ...]` | 157 | 5.6% |
| `ENV[]` references | 133 | 4.7% |
| `if`/`unless` blocks | 113 | 4.0% |
| `source :rubygems`/`:gemcutter` | 66 | 2.3% |
| `github:` gem key | 65 | 2.3% |
| `gem ... if/unless` (trailing) | 62 | 2.2% |
| `platforms`/`platform` blocks | 58 | 2.1% |
| `path:` gem key | 57 | 2.0% |
| `git:` gem key | 50 | 1.8% |
| `platforms:` keyword arg | 36 | 1.3% |
| `require: nil` | 35 | 1.2% |
| `eval_gemfile` | 32 | 1.1% |
| `%i[]`/`%w[]` percent-literals | 13 | 0.5% |
| Parenthesised calls | 12 | 0.4% |
| `groups:` (plural keyword) | 5 | 0.2% |
| `group 'name'` (string) | 4 | 0.1% |
| `plugin` directive | 2 | 0.1% |
| `ruby file:` | 1 | 0.0% |

---

## Passing File Composition

Of the 2,110 files that pass:

| Characteristic | Count |
|---|---:|
| Minimal (source + gemspec, no gem/group/ruby lines) | 1,192 |
| Have `gem` declarations | 718 |
| Have `group` blocks | 362 |
| Have `ruby` version | 16 |

The majority of passing files (56.5%) are trivial `source + gemspec` stubs.
The parser is doing real work on 918 files (43.5% of passes).

---

## Path to 100% Fidelity

### Tier 1 — Grammar additions (pure parser work, no new token types)

| Fix | Recovers | Effort |
|---|---:|---|
| `source :rubygems` / `:gemcutter` → legacy source | 58 | Trivial |
| `require: nil` → accept nil literal | 19 | Trivial |
| `group 'test' do` → STRING in group | 3 | Trivial |
| `gemspec name: "x"` / hashrocket opts | 10 | Trivial |
| `plugin "name"` → new `plugin_stmt` | 2 | Trivial |
| `ruby file: ".ruby-version"` | 1 | Trivial |
| **Subtotal** | **~93** | |

### Tier 2 — New lexer tokens + grammar rules

| Fix | Recovers | Effort |
|---|---:|---|
| `git_source(:symbol) { \|var\| "url" }` → parse and store | 335 | Medium |
| `platforms :sym do ... end` → block rule | 24 | Medium |
| `source "url" do ... end` → scoped source block | 11 | Medium |
| `[` `]` for array literals (platforms, groups) | 34 | Medium |
| `(` `)` for paren-style calls | 6 | Easy |
| `=begin...=end` block comments | 1 | Easy |
| `%i[...]` / `%w[...]` percent-literals | 13 | Medium |
| `groups:` (plural) keyword arg | 5 | Easy |
| `nil` literal token | 19 | Easy (part of Tier 1) |
| **Subtotal** | **~429** | |

### Tier 3 — Conditional / dynamic Ruby (the hard ones)

| Pattern | Files | Strategy |
|---|---:|---|
| `gem "x" if/unless COND` | 52 | Parse the gem part, flag conditional |
| `if ENV[...] ... end` blocks | 49 | Parse gem lines inside, flag conditional |
| `eval_gemfile "path"` | 15 | Follow include, parse recursively |
| Arbitrary Ruby (`def`, `require`, assignments, `case`, ternary) | ~63 | Emit warning, continue parsing remainder |
| **Subtotal** | **~179** | |

### Projected pass rates

| After implementing | Pass | % |
|---|---:|---:|
| Current | 2,110 | 75.0% |
| + Tier 1 (grammar additions) | ~2,203 | 78.3% |
| + Tier 2 (new tokens + rules) | ~2,632 | 93.6% |
| + Tier 3a (trailing conditionals + eval_gemfile) | ~2,699 | 96.0% |
| + Tier 3b (conditional blocks, skip-and-continue) | ~2,749 | 97.8% |
| Theoretical ceiling (excl. truly unparseable Ruby) | ~2,749 | 97.8% |

The remaining ~63 files (2.2%) contain top-level Ruby code (`def`, `require`,
`File.*`, variable assignments) that cannot be parsed without evaluating Ruby.
For these, the strategy should be: **warn and continue**, parsing whatever
gem/source/group lines we *can* find, rather than aborting on first encounter.

With skip-and-continue error recovery, we could potentially extract *some*
useful information from all 2,812 files (100% attempted, ~97.8% with full
fidelity, ~2.2% partial with warnings).

---

## Recommendations for Implementation Priority

Ordered by **impact on high-download gems**, not raw file count.

1. **Trailing `if`/`unless` on gem/ruby lines** — 14 of top-100 failures.
   Parse the gem declaration, store the condition as metadata (or flag it as
   conditional). This is the single biggest blocker for popular gems.

2. **`git_source` parsing** — 9 of top-100 failures, 335 total. Needed for
   `github:` gem source resolution. Every `git_source(:name)` registers a URL
   template; nearly always GitHub. Parse and store the registered source name
   and URL pattern. 65 files use `github:` keys that depend on this.

3. **`eval_gemfile`** — 7 of top-100 failures, 32 total. Recursive include
   of another Gemfile. Must follow the include and parse the referenced file.

4. **Array literals `[` `]` + `%i[]`** — 3 of top-100 failures, 47 total.
   Required for `platforms: [:mri, :ruby]` and `group: [:dev, :test]`.

5. **`gemspec` without leading comma** — 2 of top-100 failures. The grammar
   requires `gemspec COMMA KEY STRING` but real Gemfiles write `gemspec path:
   "."` with no comma (it's the first/only arg, not a continuation). Also
   need `gemspec name: "x"` and hashrocket form.

6. **`platforms`/`platform` blocks** — 1 of top-100 failures, 24 total.
   Similar structure to `group` blocks but platform-conditional.

7. **Legacy `source :rubygems`** — 0 of top-100, but 58 total. Trivial
   grammar addition.

8. **`require: nil`** — 0 of top-100, but 19 total. Add `nil` token.

9. **Parenthesised calls** — 6 total. Tokenise `(` and `)`.

10. **Error recovery** — instead of aborting on first error, skip the current
    statement and continue parsing. This is critical for the ~63 files with
    embedded Ruby code: we should extract whatever gem/source/group lines we
    *can* parse rather than giving up entirely.
