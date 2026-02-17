/*
 * tests/gemfile_test.c -- Gemfile lexer + parser tests
 *
 * All fixtures are embedded string constants -- no file I/O needed.
 *
 * Run via: make test-gemfile
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wow/gemfile.h"
#include "parser.h"  /* token IDs for lex tests */

static int n_pass, n_fail;

static void check(const char *name, int condition) {
    if (condition) {
        printf("  PASS: %s\n", name);
        n_pass++;
    } else {
        printf("  FAIL: %s\n", name);
        n_fail++;
    }
}

/* ── Helpers ────────────────────────────────────────────────────── */

static int parse(const char *src, struct wow_gemfile *gf)
{
    return wow_gemfile_parse_buf(src, (int)strlen(src), gf);
}

/* ── Test: basic source + gems ──────────────────────────────────── */

static const char BASIC[] =
    "source \"https://rubygems.org\"\n"
    "gem \"sinatra\"\n"
    "gem \"rack\"\n";

static void test_basic(void)
{
    printf("test_basic:\n");
    struct wow_gemfile gf;
    int rc = parse(BASIC, &gf);
    check("parses OK", rc == 0);
    check("source", gf.source && strcmp(gf.source, "https://rubygems.org") == 0);
    check("2 deps", gf.n_deps == 2);
    check("dep[0] = sinatra", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "sinatra") == 0);
    check("dep[1] = rack", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "rack") == 0);
    check("no constraints", gf.n_deps > 0 && gf.deps[0].n_constraints == 0);
    check("require defaults true", gf.n_deps > 0 && gf.deps[0].require == true);
    check("no ruby version", gf.ruby_version == NULL);
    check("no gemspec", gf.has_gemspec == false);
    wow_gemfile_free(&gf);
}

/* ── Test: version constraints ──────────────────────────────────── */

static const char CONSTRAINTS[] =
    "source \"https://rubygems.org\"\n"
    "gem \"sinatra\", \"~> 4.0\"\n"
    "gem \"rack\", \">= 3.0.0\", \"< 4\"\n"
    "gem \"puma\", \"~> 6.0\", \">= 6.0.2\"\n";

static void test_constraints(void)
{
    printf("test_constraints:\n");
    struct wow_gemfile gf;
    int rc = parse(CONSTRAINTS, &gf);
    check("parses OK", rc == 0);
    check("3 deps", gf.n_deps == 3);

    /* sinatra ~> 4.0 */
    check("sinatra has 1 constraint",
          gf.n_deps > 0 && gf.deps[0].n_constraints == 1);
    check("sinatra ~> 4.0",
          gf.n_deps > 0 && gf.deps[0].n_constraints > 0 &&
          strcmp(gf.deps[0].constraints[0], "~> 4.0") == 0);

    /* rack >= 3.0.0, < 4 */
    check("rack has 2 constraints",
          gf.n_deps > 1 && gf.deps[1].n_constraints == 2);
    check("rack >= 3.0.0",
          gf.n_deps > 1 && gf.deps[1].n_constraints > 0 &&
          strcmp(gf.deps[1].constraints[0], ">= 3.0.0") == 0);
    check("rack < 4",
          gf.n_deps > 1 && gf.deps[1].n_constraints > 1 &&
          strcmp(gf.deps[1].constraints[1], "< 4") == 0);

    /* puma ~> 6.0, >= 6.0.2 */
    check("puma has 2 constraints",
          gf.n_deps > 2 && gf.deps[2].n_constraints == 2);

    wow_gemfile_free(&gf);
}

/* ── Test: group blocks ─────────────────────────────────────────── */

static const char GROUPS[] =
    "source \"https://rubygems.org\"\n"
    "gem \"sinatra\", \"~> 4.0\"\n"
    "group :development do\n"
    "  gem \"rspec\", \"~> 3.0\"\n"
    "  gem \"pry\"\n"
    "end\n"
    "group :test do\n"
    "  gem \"minitest\"\n"
    "end\n"
    "gem \"rack\"\n";

static void test_groups(void)
{
    printf("test_groups:\n");
    struct wow_gemfile gf;
    int rc = parse(GROUPS, &gf);
    check("parses OK", rc == 0);
    check("5 deps", gf.n_deps == 5);

    /* sinatra — no group */
    check("sinatra no group",
          gf.n_deps > 0 && gf.deps[0].group == NULL);

    /* rspec — group: development */
    check("rspec group=development",
          gf.n_deps > 1 && gf.deps[1].group &&
          strcmp(gf.deps[1].group, "development") == 0);

    /* pry — group: development */
    check("pry group=development",
          gf.n_deps > 2 && gf.deps[2].group &&
          strcmp(gf.deps[2].group, "development") == 0);

    /* minitest — group: test */
    check("minitest group=test",
          gf.n_deps > 3 && gf.deps[3].group &&
          strcmp(gf.deps[3].group, "test") == 0);

    /* rack — no group (after end) */
    check("rack no group",
          gf.n_deps > 4 && gf.deps[4].group == NULL);

    wow_gemfile_free(&gf);
}

/* ── Test: keyword options ──────────────────────────────────────── */

static const char OPTIONS[] =
    "source \"https://rubygems.org\"\n"
    "gem \"pry\", require: false\n"
    "gem \"debug\", require: false, group: :development\n"
    "gem \"sinatra\", \"~> 4.0\", require: true\n";

static void test_options(void)
{
    printf("test_options:\n");
    struct wow_gemfile gf;
    int rc = parse(OPTIONS, &gf);
    check("parses OK", rc == 0);
    check("3 deps", gf.n_deps == 3);

    check("pry require=false",
          gf.n_deps > 0 && gf.deps[0].require == false);
    check("pry no group",
          gf.n_deps > 0 && gf.deps[0].group == NULL);

    check("debug require=false",
          gf.n_deps > 1 && gf.deps[1].require == false);
    check("debug group=development",
          gf.n_deps > 1 && gf.deps[1].group &&
          strcmp(gf.deps[1].group, "development") == 0);

    check("sinatra require=true",
          gf.n_deps > 2 && gf.deps[2].require == true);

    wow_gemfile_free(&gf);
}

/* ── Test: hashrocket syntax ────────────────────────────────────── */

static const char HASHROCKET_SYN[] =
    "source \"https://rubygems.org\"\n"
    "gem \"pry\", :require => false, :group => :development\n";

static void test_hashrocket(void)
{
    printf("test_hashrocket:\n");
    struct wow_gemfile gf;
    int rc = parse(HASHROCKET_SYN, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("require=false",
          gf.n_deps > 0 && gf.deps[0].require == false);
    check("group=development",
          gf.n_deps > 0 && gf.deps[0].group &&
          strcmp(gf.deps[0].group, "development") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: ruby version ─────────────────────────────────────────── */

static const char RUBY_VER[] =
    "source \"https://rubygems.org\"\n"
    "ruby \"3.3.0\"\n"
    "gem \"sinatra\"\n";

static void test_ruby_version(void)
{
    printf("test_ruby_version:\n");
    struct wow_gemfile gf;
    int rc = parse(RUBY_VER, &gf);
    check("parses OK", rc == 0);
    check("ruby 3.3.0",
          gf.ruby_version && strcmp(gf.ruby_version, "3.3.0") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: gemspec ──────────────────────────────────────────────── */

static const char GEMSPEC_SRC[] =
    "source \"https://rubygems.org\"\n"
    "gemspec\n"
    "gem \"rspec\", \"~> 3.0\", group: :test\n";

static void test_gemspec(void)
{
    printf("test_gemspec:\n");
    struct wow_gemfile gf;
    int rc = parse(GEMSPEC_SRC, &gf);
    check("parses OK", rc == 0);
    check("has_gemspec", gf.has_gemspec == true);
    check("1 dep", gf.n_deps == 1);
    wow_gemfile_free(&gf);
}

/* ── Test: comments and blank lines ─────────────────────────────── */

static const char COMMENTS[] =
    "# frozen_string_literal: true\n"
    "\n"
    "# This is a comment\n"
    "source \"https://rubygems.org\"\n"
    "\n"
    "# Another comment\n"
    "gem \"sinatra\", \"~> 4.0\"\n"
    "\n";

static void test_comments(void)
{
    printf("test_comments:\n");
    struct wow_gemfile gf;
    int rc = parse(COMMENTS, &gf);
    check("parses OK", rc == 0);
    check("source parsed", gf.source &&
          strcmp(gf.source, "https://rubygems.org") == 0);
    check("1 dep", gf.n_deps == 1);
    wow_gemfile_free(&gf);
}

/* ── Test: unsupported syntax → error ───────────────────────────── */

static const char UNSUPPORTED_IF[] =
    "source \"https://rubygems.org\"\n"
    "if RUBY_VERSION >= \"3.0\"\n"
    "  gem \"new-gem\"\n"
    "end\n";

static const char UNSUPPORTED_EVAL[] =
    "source \"https://rubygems.org\"\n"
    "eval_gemfile \"other.rb\"\n";

static void test_unsupported(void)
{
    printf("test_unsupported:\n");
    struct wow_gemfile gf;

    /* Redirect stderr to /dev/null for clean test output */
    FILE *saved_stderr = stderr;
    stderr = fopen("/dev/null", "w");

    int rc1 = parse(UNSUPPORTED_IF, &gf);
    check("if rejected", rc1 != 0);

    int rc2 = parse(UNSUPPORTED_EVAL, &gf);
    check("eval_gemfile rejected", rc2 != 0);

    fclose(stderr);
    stderr = saved_stderr;
}

/* ── Test: empty Gemfile ────────────────────────────────────────── */

static const char EMPTY[] = "";

static const char ONLY_COMMENTS[] =
    "# just a comment\n"
    "\n"
    "# another comment\n";

static void test_empty(void)
{
    printf("test_empty:\n");
    struct wow_gemfile gf;

    int rc1 = parse(EMPTY, &gf);
    check("empty parses OK", rc1 == 0);
    check("empty: 0 deps", gf.n_deps == 0);
    check("empty: no source", gf.source == NULL);
    wow_gemfile_free(&gf);

    int rc2 = parse(ONLY_COMMENTS, &gf);
    check("comments-only parses OK", rc2 == 0);
    check("comments-only: 0 deps", gf.n_deps == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: multi-symbol group ───────────────────────────────────── */

static const char MULTI_GROUP[] =
    "source \"https://rubygems.org\"\n"
    "group :development, :test do\n"
    "  gem \"rspec\"\n"
    "end\n";

static void test_multi_group(void)
{
    printf("test_multi_group:\n");
    struct wow_gemfile gf;
    int rc = parse(MULTI_GROUP, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    /* First symbol is kept */
    check("group=development",
          gf.n_deps > 0 && gf.deps[0].group &&
          strcmp(gf.deps[0].group, "development") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: single-quoted strings ────────────────────────────────── */

static const char SINGLE_QUOTES[] =
    "source 'https://rubygems.org'\n"
    "gem 'sinatra', '~> 4.0'\n";

static void test_single_quotes(void)
{
    printf("test_single_quotes:\n");
    struct wow_gemfile gf;
    int rc = parse(SINGLE_QUOTES, &gf);
    check("parses OK", rc == 0);
    check("source", gf.source &&
          strcmp(gf.source, "https://rubygems.org") == 0);
    check("sinatra ~> 4.0",
          gf.n_deps > 0 && gf.deps[0].n_constraints > 0 &&
          strcmp(gf.deps[0].constraints[0], "~> 4.0") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: unknown keys accepted but ignored ────────────────────── */

static const char UNKNOWN_KEYS[] =
    "source \"https://rubygems.org\"\n"
    "gem \"debug\", platforms: :mri\n"
    "gem \"local\", path: \"./lib\"\n"
    "gem \"remote\", git: \"https://github.com/foo/bar\"\n";

static void test_unknown_keys(void)
{
    printf("test_unknown_keys:\n");
    struct wow_gemfile gf;
    int rc = parse(UNKNOWN_KEYS, &gf);
    check("parses OK", rc == 0);
    check("3 deps", gf.n_deps == 3);
    check("debug parsed", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "debug") == 0);
    check("local parsed", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "local") == 0);
    check("remote parsed", gf.n_deps > 2 &&
          strcmp(gf.deps[2].name, "remote") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: group: keyword overrides block group ─────────────────── */

static const char GROUP_OVERRIDE[] =
    "source \"https://rubygems.org\"\n"
    "group :development do\n"
    "  gem \"pry\"\n"
    "  gem \"debug\", group: :test\n"
    "end\n";

static void test_group_override(void)
{
    printf("test_group_override:\n");
    struct wow_gemfile gf;
    int rc = parse(GROUP_OVERRIDE, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("pry inherits block group",
          gf.n_deps > 0 && gf.deps[0].group &&
          strcmp(gf.deps[0].group, "development") == 0);
    check("debug explicit group overrides block",
          gf.n_deps > 1 && gf.deps[1].group &&
          strcmp(gf.deps[1].group, "test") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: source :rubygems (legacy symbol) ─────────────────────── */

static const char SOURCE_SYMBOL[] =
    "source :rubygems\n"
    "gem \"rake\"\n";

static void test_source_symbol(void)
{
    printf("test_source_symbol:\n");
    struct wow_gemfile gf;
    int rc = parse(SOURCE_SYMBOL, &gf);
    check("parses OK", rc == 0);
    check("source mapped to rubygems.org",
          gf.source && strcmp(gf.source, "https://rubygems.org") == 0);
    check("1 dep", gf.n_deps == 1);
    wow_gemfile_free(&gf);
}

/* ── Test: source :gemcutter (legacy alias) ─────────────────────── */

static const char SOURCE_GEMCUTTER[] =
    "source :gemcutter\n"
    "gemspec\n";

static void test_source_gemcutter(void)
{
    printf("test_source_gemcutter:\n");
    struct wow_gemfile gf;
    int rc = parse(SOURCE_GEMCUTTER, &gf);
    check("parses OK", rc == 0);
    check("source mapped to rubygems.org",
          gf.source && strcmp(gf.source, "https://rubygems.org") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: require: nil ─────────────────────────────────────────── */

static const char REQUIRE_NIL[] =
    "source \"https://rubygems.org\"\n"
    "gem \"rake\", require: nil\n"
    "gem \"pg\", \"~> 1.5\", :require => nil\n";

static void test_require_nil(void)
{
    printf("test_require_nil:\n");
    struct wow_gemfile gf;
    int rc = parse(REQUIRE_NIL, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("rake require=false", gf.n_deps > 0 && gf.deps[0].require == false);
    check("pg require=false", gf.n_deps > 1 && gf.deps[1].require == false);
    wow_gemfile_free(&gf);
}

/* ── Test: group 'test' do (string name) ────────────────────────── */

static const char GROUP_STRING[] =
    "source \"https://rubygems.org\"\n"
    "gemspec\n"
    "group 'test' do\n"
    "  gem \"simplecov\", require: false\n"
    "end\n";

static void test_group_string(void)
{
    printf("test_group_string:\n");
    struct wow_gemfile gf;
    int rc = parse(GROUP_STRING, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("group=test",
          gf.n_deps > 0 && gf.deps[0].group &&
          strcmp(gf.deps[0].group, "test") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: gemspec path: "." (no leading comma) ─────────────────── */

static const char GEMSPEC_PATH[] =
    "source \"https://rubygems.org\"\n"
    "gemspec path: \".\"\n";

static void test_gemspec_path(void)
{
    printf("test_gemspec_path:\n");
    struct wow_gemfile gf;
    int rc = parse(GEMSPEC_PATH, &gf);
    check("parses OK", rc == 0);
    check("has_gemspec", gf.has_gemspec == true);
    wow_gemfile_free(&gf);
}

/* ── Test: gemspec :path => "lib" (hashrocket) ──────────────────── */

static const char GEMSPEC_HASHROCKET[] =
    "source \"https://rubygems.org\"\n"
    "gemspec :path => \"lib\"\n";

static void test_gemspec_hashrocket(void)
{
    printf("test_gemspec_hashrocket:\n");
    struct wow_gemfile gf;
    int rc = parse(GEMSPEC_HASHROCKET, &gf);
    check("parses OK", rc == 0);
    check("has_gemspec", gf.has_gemspec == true);
    wow_gemfile_free(&gf);
}

/* ── Test: plugin "name", "version" ─────────────────────────────── */

static const char PLUGIN_SRC[] =
    "plugin \"bundler-multilock\", \"1.3.4\"\n"
    "source \"https://rubygems.org\"\n"
    "gem \"sinatra\"\n";

static void test_plugin(void)
{
    printf("test_plugin:\n");
    struct wow_gemfile gf;
    int rc = parse(PLUGIN_SRC, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    wow_gemfile_free(&gf);
}

/* ── Test: ruby file: ".ruby-version" ───────────────────────────── */

static const char RUBY_FILE[] =
    "source \"https://rubygems.org\"\n"
    "ruby file: \".ruby-version\"\n"
    "gem \"sinatra\"\n";

static void test_ruby_file(void)
{
    printf("test_ruby_file:\n");
    struct wow_gemfile gf;
    int rc = parse(RUBY_FILE, &gf);
    check("parses OK", rc == 0);
    check("no ruby version extracted", gf.ruby_version == NULL);
    check("1 dep", gf.n_deps == 1);
    wow_gemfile_free(&gf);
}

/* ── Test: git_source brace form ────────────────────────────────── */

static const char GIT_SOURCE_BRACE[] =
    "source \"https://rubygems.org\"\n"
    "git_source(:github) { |repo| \"https://github.com/#{repo}.git\" }\n"
    "gemspec\n";

static void test_git_source_brace(void)
{
    printf("test_git_source_brace:\n");
    struct wow_gemfile gf;
    int rc = parse(GIT_SOURCE_BRACE, &gf);
    check("parses OK", rc == 0);
    check("has_gemspec", gf.has_gemspec == true);
    wow_gemfile_free(&gf);
}

/* ── Test: git_source do...end form ─────────────────────────────── */

static const char GIT_SOURCE_DO_END[] =
    "source \"https://rubygems.org\"\n"
    "git_source(:github) do |repo_name|\n"
    "  \"https://github.com/#{repo_name}\"\n"
    "end\n"
    "gemspec\n";

static void test_git_source_do_end(void)
{
    printf("test_git_source_do_end:\n");
    struct wow_gemfile gf;
    int rc = parse(GIT_SOURCE_DO_END, &gf);
    check("parses OK", rc == 0);
    check("has_gemspec", gf.has_gemspec == true);
    wow_gemfile_free(&gf);
}

/* ── Test: git_source no-paren form ─────────────────────────────── */

static const char GIT_SOURCE_NO_PARENS[] =
    "source 'https://rubygems.org'\n"
    "git_source :github do |repo_name|\n"
    "  \"https://github.com/#{repo_name}\"\n"
    "end\n"
    "gemspec\n";

static void test_git_source_no_parens(void)
{
    printf("test_git_source_no_parens:\n");
    struct wow_gemfile gf;
    int rc = parse(GIT_SOURCE_NO_PARENS, &gf);
    check("parses OK", rc == 0);
    check("has_gemspec", gf.has_gemspec == true);
    wow_gemfile_free(&gf);
}

/* ── Test: platforms :ruby do ... end ───────────────────────────── */

static const char PLATFORMS_BLOCK[] =
    "source \"http://rubygems.org\"\n"
    "gemspec\n"
    "platform :jruby do\n"
    "  gem \"jruby-openssl\"\n"
    "end\n"
    "platforms :ruby do\n"
    "  gem \"sqlite3\"\n"
    "end\n";

static void test_platforms_block(void)
{
    printf("test_platforms_block:\n");
    struct wow_gemfile gf;
    int rc = parse(PLATFORMS_BLOCK, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("no group set on jruby-openssl",
          gf.n_deps > 0 && gf.deps[0].group == NULL);
    check("no group set on sqlite3",
          gf.n_deps > 1 && gf.deps[1].group == NULL);
    wow_gemfile_free(&gf);
}

/* ── Test: source "url" do ... end (scoped source block) ────────── */

static const char SOURCE_BLOCK[] =
    "source 'https://rubygems.org'\n"
    "source 'https://rails-assets.org' do\n"
    "  gem 'rails-assets-growl', '~> 1.3.1'\n"
    "end\n"
    "gemspec\n";

static void test_source_block(void)
{
    printf("test_source_block:\n");
    struct wow_gemfile gf;
    int rc = parse(SOURCE_BLOCK, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("has_gemspec", gf.has_gemspec == true);
    wow_gemfile_free(&gf);
}

/* ── Test: array literal [:dev, :test] ──────────────────────────── */

static const char ARRAY_LITERAL[] =
    "source \"https://rubygems.org\"\n"
    "gem \"byebug\", group: [:development, :test]\n"
    "gem \"debug\", platforms: [:mri, :mingw, :x64_mingw]\n";

static void test_array_literal(void)
{
    printf("test_array_literal:\n");
    struct wow_gemfile gf;
    int rc = parse(ARRAY_LITERAL, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    wow_gemfile_free(&gf);
}

/* ── Test: %i[...] percent-literal array ────────────────────────── */

static const char PERCENT_ARRAY_SRC[] =
    "source \"https://rubygems.org\"\n"
    "gem \"debug\", platforms: %i[mri windows]\n"
    "gem \"tzinfo-data\", platforms: %i[mingw mswin x64_mingw jruby]\n";

static void test_percent_array(void)
{
    printf("test_percent_array:\n");
    struct wow_gemfile gf;
    int rc = parse(PERCENT_ARRAY_SRC, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    wow_gemfile_free(&gf);
}

/* ── Test: source("url") parenthesised call ─────────────────────── */

static const char PAREN_SOURCE[] =
    "source('https://rubygems.org')\n"
    "gem \"rake\"\n";

static void test_paren_source(void)
{
    printf("test_paren_source:\n");
    struct wow_gemfile gf;
    int rc = parse(PAREN_SOURCE, &gf);
    check("parses OK", rc == 0);
    check("source", gf.source &&
          strcmp(gf.source, "https://rubygems.org") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: =begin...=end block comment ──────────────────────────── */

static const char BLOCK_COMMENT[] =
    "=begin\n"
    "  This is a block comment.\n"
    "=end\n"
    "source \"http://rubygems.org\"\n"
    "gem \"rake\"\n";

static void test_block_comment(void)
{
    printf("test_block_comment:\n");
    struct wow_gemfile gf;
    int rc = parse(BLOCK_COMMENT, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    wow_gemfile_free(&gf);
}

/* ── Test: groups: plural keyword ───────────────────────────────── */

static const char GROUPS_PLURAL[] =
    "source \"https://rubygems.org\"\n"
    "gem \"webpacker\", groups: [:production, :development]\n"
    "gem \"guard\", groups: :test\n";

static void test_groups_plural(void)
{
    printf("test_groups_plural:\n");
    struct wow_gemfile gf;
    int rc = parse(GROUPS_PLURAL, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("guard group=test",
          gf.n_deps > 1 && gf.deps[1].group &&
          strcmp(gf.deps[1].group, "test") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: nested platforms inside group ─────────────────────────── */

static const char NESTED_BLOCKS[] =
    "source \"https://rubygems.org\"\n"
    "group :development do\n"
    "  gem \"pry\"\n"
    "  platforms :mri do\n"
    "    gem \"byebug\"\n"
    "  end\n"
    "end\n";

static void test_nested_blocks(void)
{
    printf("test_nested_blocks:\n");
    struct wow_gemfile gf;
    int rc = parse(NESTED_BLOCKS, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("pry group=development",
          gf.n_deps > 0 && gf.deps[0].group &&
          strcmp(gf.deps[0].group, "development") == 0);
    check("byebug inherits group=development",
          gf.n_deps > 1 && gf.deps[1].group &&
          strcmp(gf.deps[1].group, "development") == 0);
    wow_gemfile_free(&gf);
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== wow Gemfile parser tests ===\n\n");

    test_basic();
    test_constraints();
    test_groups();
    test_options();
    test_hashrocket();
    test_ruby_version();
    test_gemspec();
    test_comments();
    test_unsupported();
    test_empty();
    test_multi_group();
    test_single_quotes();
    test_unknown_keys();
    test_group_override();

    /* Tier 1 + Tier 2 tests */
    test_source_symbol();
    test_source_gemcutter();
    test_require_nil();
    test_group_string();
    test_gemspec_path();
    test_gemspec_hashrocket();
    test_plugin();
    test_ruby_file();
    test_git_source_brace();
    test_git_source_do_end();
    test_git_source_no_parens();
    test_platforms_block();
    test_source_block();
    test_array_literal();
    test_percent_array();
    test_paren_source();
    test_block_comment();
    test_groups_plural();
    test_nested_blocks();

    printf("\n=== Results: %d passed, %d failed ===\n", n_pass, n_fail);
    return n_fail > 0 ? 1 : 0;
}
