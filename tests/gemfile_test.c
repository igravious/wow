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

static const char UNSUPPORTED_CASE[] =
    "source \"https://rubygems.org\"\n"
    "case RUBY_ENGINE\n"
    "when \"ruby\"\n"
    "  gem \"new-gem\"\n"
    "end\n";

static const char UNSUPPORTED_DEF[] =
    "source \"https://rubygems.org\"\n"
    "def my_gems\n"
    "  gem \"sinatra\"\n"
    "end\n";

static void test_unsupported(void)
{
    printf("test_unsupported:\n");
    struct wow_gemfile gf;

    /* Redirect stderr to /dev/null for clean test output */
    FILE *saved_stderr = stderr;
    stderr = fopen("/dev/null", "w");

    int rc1 = parse(UNSUPPORTED_CASE, &gf);
    check("case/when rejected", rc1 != 0);

    int rc2 = parse(UNSUPPORTED_DEF, &gf);
    check("def rejected", rc2 != 0);

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

/* ═══════════════════════════════════════════════════════════════════ */
/* Evaluator tests (Phase 5b — if/unless/elsif/else, ENV, variables) */
/* ═══════════════════════════════════════════════════════════════════ */

/* ── Test: if true includes gems ────────────────────────────────── */

static const char EVAL_IF_TRUE[] =
    "source \"https://rubygems.org\"\n"
    "if true\n"
    "  gem \"inside\"\n"
    "end\n"
    "gem \"outside\"\n";

static void test_eval_if_true(void)
{
    printf("test_eval_if_true:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_IF_TRUE, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("dep[0] = inside", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "inside") == 0);
    check("dep[1] = outside", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "outside") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: if false skips gems ──────────────────────────────────── */

static const char EVAL_IF_FALSE[] =
    "source \"https://rubygems.org\"\n"
    "if false\n"
    "  gem \"skipped\"\n"
    "end\n"
    "gem \"kept\"\n";

static void test_eval_if_false(void)
{
    printf("test_eval_if_false:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_IF_FALSE, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = kept", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "kept") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: unless ───────────────────────────────────────────────── */

static const char EVAL_UNLESS[] =
    "source \"https://rubygems.org\"\n"
    "unless false\n"
    "  gem \"included\"\n"
    "end\n"
    "unless true\n"
    "  gem \"excluded\"\n"
    "end\n";

static void test_eval_unless(void)
{
    printf("test_eval_unless:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_UNLESS, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = included", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "included") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: if/elsif/else branch selection ───────────────────────── */

static const char EVAL_ELSIF[] =
    "source \"https://rubygems.org\"\n"
    "if false\n"
    "  gem \"branch1\"\n"
    "elsif true\n"
    "  gem \"branch2\"\n"
    "else\n"
    "  gem \"branch3\"\n"
    "end\n";

static void test_eval_elsif(void)
{
    printf("test_eval_elsif:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_ELSIF, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = branch2", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "branch2") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: if false / else takes else ───────────────────────────── */

static const char EVAL_ELSE[] =
    "source \"https://rubygems.org\"\n"
    "if false\n"
    "  gem \"nope\"\n"
    "else\n"
    "  gem \"yep\"\n"
    "end\n";

static void test_eval_else(void)
{
    printf("test_eval_else:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_ELSE, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = yep", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "yep") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: nested if inside group do/end ────────────────────────── */

static const char EVAL_NESTED_IF[] =
    "source \"https://rubygems.org\"\n"
    "group :development do\n"
    "  gem \"pry\"\n"
    "  if true\n"
    "    gem \"debug\"\n"
    "  end\n"
    "  if false\n"
    "    gem \"byebug\"\n"
    "  end\n"
    "end\n";

static void test_eval_nested_if(void)
{
    printf("test_eval_nested_if:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_NESTED_IF, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("dep[0] = pry", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "pry") == 0);
    check("dep[1] = debug", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "debug") == 0);
    check("pry group=development", gf.n_deps > 0 && gf.deps[0].group &&
          strcmp(gf.deps[0].group, "development") == 0);
    check("debug group=development", gf.n_deps > 1 && gf.deps[1].group &&
          strcmp(gf.deps[1].group, "development") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: trailing if true → gem included ──────────────────────── */

static const char EVAL_TRAILING_IF_TRUE[] =
    "source \"https://rubygems.org\"\n"
    "gem \"always\"\n"
    "gem \"conditional\" if true\n";

static void test_eval_trailing_if_true(void)
{
    printf("test_eval_trailing_if_true:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_TRAILING_IF_TRUE, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("dep[0] = always", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "always") == 0);
    check("dep[1] = conditional", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "conditional") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: trailing if false → gem excluded ─────────────────────── */

static const char EVAL_TRAILING_IF_FALSE[] =
    "source \"https://rubygems.org\"\n"
    "gem \"always\"\n"
    "gem \"conditional\" if false\n";

static void test_eval_trailing_if_false(void)
{
    printf("test_eval_trailing_if_false:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_TRAILING_IF_FALSE, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = always", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "always") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: trailing unless true → gem excluded ──────────────────── */

static const char EVAL_TRAILING_UNLESS[] =
    "source \"https://rubygems.org\"\n"
    "gem \"yes\" unless false\n"
    "gem \"no\" unless true\n";

static void test_eval_trailing_unless(void)
{
    printf("test_eval_trailing_unless:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_TRAILING_UNLESS, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = yes", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "yes") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: ENV["KEY"] exists → truthy ───────────────────────────── */

static const char EVAL_ENV_EXISTS[] =
    "source \"https://rubygems.org\"\n"
    "gem \"base\"\n"
    "gem \"env-gem\" if ENV[\"WOW_TEST_PRESENT\"]\n";

static void test_eval_env_exists(void)
{
    printf("test_eval_env_exists:\n");
    struct wow_gemfile gf;

    setenv("WOW_TEST_PRESENT", "1", 1);
    int rc = parse(EVAL_ENV_EXISTS, &gf);
    unsetenv("WOW_TEST_PRESENT");

    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("dep[1] = env-gem", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "env-gem") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: ENV["KEY"] missing → nil (falsy) ─────────────────────── */

static const char EVAL_ENV_MISSING[] =
    "source \"https://rubygems.org\"\n"
    "gem \"base\"\n"
    "gem \"env-gem\" if ENV[\"WOW_TEST_ABSENT_12345\"]\n";

static void test_eval_env_missing(void)
{
    printf("test_eval_env_missing:\n");
    struct wow_gemfile gf;

    unsetenv("WOW_TEST_ABSENT_12345");
    int rc = parse(EVAL_ENV_MISSING, &gf);

    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = base", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "base") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: ENV["KEY"] == "value" comparison ─────────────────────── */

static const char EVAL_ENV_EQ[] =
    "source \"https://rubygems.org\"\n"
    "if ENV[\"WOW_TEST_EQ\"] == \"yes\"\n"
    "  gem \"matched\"\n"
    "end\n"
    "gem \"always\"\n";

static void test_eval_env_eq(void)
{
    printf("test_eval_env_eq:\n");
    struct wow_gemfile gf;

    /* Set to matching value */
    setenv("WOW_TEST_EQ", "yes", 1);
    int rc1 = parse(EVAL_ENV_EQ, &gf);
    check("parses OK (match)", rc1 == 0);
    check("2 deps (match)", gf.n_deps == 2);
    check("dep[0] = matched", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "matched") == 0);
    wow_gemfile_free(&gf);

    /* Set to non-matching value */
    setenv("WOW_TEST_EQ", "no", 1);
    int rc2 = parse(EVAL_ENV_EQ, &gf);
    check("parses OK (no match)", rc2 == 0);
    check("1 dep (no match)", gf.n_deps == 1);
    check("dep[0] = always", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "always") == 0);
    wow_gemfile_free(&gf);

    unsetenv("WOW_TEST_EQ");
}

/* ── Test: RUBY_VERSION >= "3.0" ────────────────────────────────── */

static const char EVAL_RUBY_VER[] =
    "source \"https://rubygems.org\"\n"
    "if RUBY_VERSION >= \"3.0\"\n"
    "  gem \"modern\"\n"
    "end\n"
    "if RUBY_VERSION < \"2.0\"\n"
    "  gem \"ancient\"\n"
    "end\n";

static void test_eval_ruby_version_cmp(void)
{
    printf("test_eval_ruby_version_cmp:\n");
    struct wow_gemfile gf;
    /* Default RUBY_VERSION is "3.3.0" */
    int rc = parse(EVAL_RUBY_VER, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = modern", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "modern") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: variable assignment + conditional ────────────────────── */

static const char EVAL_VAR_ASSIGN[] =
    "source \"https://rubygems.org\"\n"
    "want_debug = true\n"
    "gem \"debug\" if want_debug\n"
    "gem \"always\"\n";

static void test_eval_variable_assign(void)
{
    printf("test_eval_variable_assign:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_VAR_ASSIGN, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("dep[0] = debug", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "debug") == 0);
    check("dep[1] = always", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "always") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: variable from ENV ────────────────────────────────────── */

static const char EVAL_VAR_ENV[] =
    "source \"https://rubygems.org\"\n"
    "ci = ENV[\"WOW_TEST_CI\"]\n"
    "gem \"ci-reporter\" if ci\n"
    "gem \"always\"\n";

static void test_eval_variable_env(void)
{
    printf("test_eval_variable_env:\n");
    struct wow_gemfile gf;

    /* CI set */
    setenv("WOW_TEST_CI", "1", 1);
    int rc1 = parse(EVAL_VAR_ENV, &gf);
    check("parses OK (ci set)", rc1 == 0);
    check("2 deps (ci set)", gf.n_deps == 2);
    check("dep[0] = ci-reporter", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "ci-reporter") == 0);
    wow_gemfile_free(&gf);

    /* CI unset */
    unsetenv("WOW_TEST_CI");
    int rc2 = parse(EVAL_VAR_ENV, &gf);
    check("parses OK (ci unset)", rc2 == 0);
    check("1 dep (ci unset)", gf.n_deps == 1);
    check("dep[0] = always", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "always") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: Gem::Version.new() comparison ────────────────────────── */

static const char EVAL_GEM_VERSION[] =
    "source \"https://rubygems.org\"\n"
    "if Gem::Version.new(RUBY_VERSION) >= Gem::Version.new(\"3.0.0\")\n"
    "  gem \"modern-gem\"\n"
    "end\n";

static void test_eval_gem_version(void)
{
    printf("test_eval_gem_version:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_GEM_VERSION, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = modern-gem", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "modern-gem") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: && and || operators ──────────────────────────────────── */

static const char EVAL_AND_OR[] =
    "source \"https://rubygems.org\"\n"
    "gem \"both\" if true && true\n"
    "gem \"neither\" if true && false\n"
    "gem \"either\" if false || true\n"
    "gem \"none\" if false || false\n";

static void test_eval_and_or(void)
{
    printf("test_eval_and_or:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_AND_OR, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("dep[0] = both", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "both") == 0);
    check("dep[1] = either", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "either") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: interleaved group do/end and if/end ──────────────────── */

static const char EVAL_INTERLEAVED[] =
    "source \"https://rubygems.org\"\n"
    "group :test do\n"
    "  if true\n"
    "    gem \"rspec\"\n"
    "  end\n"
    "  gem \"minitest\"\n"
    "end\n"
    "gem \"sinatra\"\n";

static void test_eval_interleaved(void)
{
    printf("test_eval_interleaved:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_INTERLEAVED, &gf);
    check("parses OK", rc == 0);
    check("3 deps", gf.n_deps == 3);
    check("dep[0] = rspec", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "rspec") == 0);
    check("rspec group=test", gf.n_deps > 0 && gf.deps[0].group &&
          strcmp(gf.deps[0].group, "test") == 0);
    check("dep[1] = minitest", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "minitest") == 0);
    check("dep[2] = sinatra", gf.n_deps > 2 &&
          strcmp(gf.deps[2].name, "sinatra") == 0);
    check("sinatra no group", gf.n_deps > 2 && gf.deps[2].group == NULL);
    wow_gemfile_free(&gf);
}

/* ── Test: if inside suppressed scope → properly tracked ────────── */

static const char EVAL_SUPPRESSED_NESTED[] =
    "source \"https://rubygems.org\"\n"
    "if false\n"
    "  if true\n"
    "    gem \"deep-inside\"\n"
    "  end\n"
    "  gem \"inside\"\n"
    "end\n"
    "gem \"outside\"\n";

static void test_eval_suppressed_nested(void)
{
    printf("test_eval_suppressed_nested:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_SUPPRESSED_NESTED, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = outside", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "outside") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: ! (not) operator ─────────────────────────────────────── */

static const char EVAL_BANG[] =
    "source \"https://rubygems.org\"\n"
    "gem \"negated\" if !false\n"
    "gem \"double\" if !true\n";

static void test_eval_bang(void)
{
    printf("test_eval_bang:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_BANG, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = negated", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "negated") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: version_compare utility ──────────────────────────────── */

static void test_version_compare(void)
{
    printf("test_version_compare:\n");
    check("3.3.0 == 3.3.0", wow_version_compare("3.3.0", "3.3.0") == 0);
    check("3.3.0 > 3.2.0", wow_version_compare("3.3.0", "3.2.0") > 0);
    check("3.3.0 < 3.4.0", wow_version_compare("3.3.0", "3.4.0") < 0);
    check("3.10.0 > 3.9.0", wow_version_compare("3.10.0", "3.9.0") > 0);
    check("2.0 < 10.0", wow_version_compare("2.0", "10.0") < 0);
    check("1.0.0 < 1.0.1", wow_version_compare("1.0.0", "1.0.1") < 0);
    check("1.0 == 1.0.0", wow_version_compare("1.0", "1.0.0") == 0);
}

/* ── Test: eval_gemfile with missing file → silently skipped ────── */

static const char EVAL_GEMFILE_MISSING[] =
    "source \"https://rubygems.org\"\n"
    "eval_gemfile \"nonexistent_file_12345.rb\"\n"
    "gem \"sinatra\"\n";

static void test_eval_gemfile_missing(void)
{
    printf("test_eval_gemfile_missing:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_GEMFILE_MISSING, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = sinatra", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "sinatra") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: string interpolation with variable ───────────────────── */

static const char EVAL_INTERPOLATION[] =
    "source \"https://rubygems.org\"\n"
    "ver = \"5.0\"\n"
    "gem \"rails\", \"~> #{ver}\"\n";

static void test_eval_interpolation(void)
{
    printf("test_eval_interpolation:\n");
    struct wow_gemfile gf;
    int rc = parse(EVAL_INTERPOLATION, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = rails", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "rails") == 0);
    check("constraint ~> 5.0", gf.n_deps > 0 &&
          gf.deps[0].n_constraints > 0 &&
          strcmp(gf.deps[0].constraints[0], "~> 5.0") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: if RUBY_VERSION (real-world pattern from corpus) ─────── */

static const char EVAL_REALWORLD[] =
    "source \"https://rubygems.org\"\n"
    "gemspec\n"
    "gem \"rake\"\n"
    "group :development do\n"
    "  gem \"pry\"\n"
    "  gem \"byebug\" if RUBY_VERSION >= \"2.0\" && RUBY_VERSION < \"4.0\"\n"
    "end\n"
    "gem \"simplecov\", require: false unless ENV[\"WOW_TEST_NO_COV\"]\n";

static void test_eval_realworld(void)
{
    printf("test_eval_realworld:\n");
    struct wow_gemfile gf;

    unsetenv("WOW_TEST_NO_COV");
    int rc = parse(EVAL_REALWORLD, &gf);

    check("parses OK", rc == 0);
    check("has_gemspec", gf.has_gemspec == true);
    check("4 deps", gf.n_deps == 4);
    check("dep[0] = rake", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "rake") == 0);
    check("dep[1] = pry", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "pry") == 0);
    check("dep[2] = byebug", gf.n_deps > 2 &&
          strcmp(gf.deps[2].name, "byebug") == 0);
    check("byebug group=development", gf.n_deps > 2 && gf.deps[2].group &&
          strcmp(gf.deps[2].group, "development") == 0);
    check("dep[3] = simplecov", gf.n_deps > 3 &&
          strcmp(gf.deps[3].name, "simplecov") == 0);
    check("simplecov require=false", gf.n_deps > 3 &&
          gf.deps[3].require == false);
    wow_gemfile_free(&gf);
}

/* ── Test: group :x, optional: true do (keyword args in group) ──── */

static const char GROUP_OPTIONAL[] =
    "source \"https://rubygems.org\"\n"
    "group :development, optional: true do\n"
    "  gem \"pry\"\n"
    "end\n";

static void test_group_optional(void)
{
    printf("test_group_optional:\n");
    struct wow_gemfile gf;
    int rc = parse(GROUP_OPTIONAL, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("pry group=development",
          gf.n_deps > 0 && gf.deps[0].group &&
          strcmp(gf.deps[0].group, "development") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: group with multiple keyword args ─────────────────────── */

static const char GROUP_MULTI_KW[] =
    "source \"https://rubygems.org\"\n"
    "group :development, :test, optional: true do\n"
    "  gem \"rspec\"\n"
    "end\n";

static void test_group_multi_kw(void)
{
    printf("test_group_multi_kw:\n");
    struct wow_gemfile gf;
    int rc = parse(GROUP_MULTI_KW, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("rspec group=development",
          gf.n_deps > 0 && gf.deps[0].group &&
          strcmp(gf.deps[0].group, "development") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: gem 'x', ['>= 1', '< 2'] array constraints ──────────── */

static const char ARRAY_CONSTRAINTS[] =
    "source \"https://rubygems.org\"\n"
    "gem \"rails\", [\"~> 7.0\", \">= 7.0.1\"]\n";

static void test_array_constraints(void)
{
    printf("test_array_constraints:\n");
    struct wow_gemfile gf;
    int rc = parse(ARRAY_CONSTRAINTS, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = rails", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "rails") == 0);
    check("2 constraints", gf.n_deps > 0 && gf.deps[0].n_constraints == 2);
    check("constraint[0] = ~> 7.0", gf.n_deps > 0 &&
          gf.deps[0].n_constraints > 0 &&
          strcmp(gf.deps[0].constraints[0], "~> 7.0") == 0);
    check("constraint[1] = >= 7.0.1", gf.n_deps > 0 &&
          gf.deps[0].n_constraints > 1 &&
          strcmp(gf.deps[0].constraints[1], ">= 7.0.1") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: gem("name", "~> 1.0") parenthesised call ─────────────── */

static const char PAREN_GEM[] =
    "source \"https://rubygems.org\"\n"
    "gem(\"sinatra\", \"~> 4.0\", require: false)\n";

static void test_paren_gem(void)
{
    printf("test_paren_gem:\n");
    struct wow_gemfile gf;
    int rc = parse(PAREN_GEM, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = sinatra", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "sinatra") == 0);
    check("constraint ~> 4.0", gf.n_deps > 0 &&
          gf.deps[0].n_constraints > 0 &&
          strcmp(gf.deps[0].constraints[0], "~> 4.0") == 0);
    check("require=false", gf.n_deps > 0 && gf.deps[0].require == false);
    wow_gemfile_free(&gf);
}

/* ── Test: path "." do ... end block ────────────────────────────── */

static const char PATH_BLOCK[] =
    "source \"https://rubygems.org\"\n"
    "path \".\" do\n"
    "  gem \"my-local-gem\"\n"
    "end\n"
    "gem \"sinatra\"\n";

static void test_path_block(void)
{
    printf("test_path_block:\n");
    struct wow_gemfile gf;
    int rc = parse(PATH_BLOCK, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("dep[0] = my-local-gem", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "my-local-gem") == 0);
    check("dep[1] = sinatra", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "sinatra") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: git "url" do ... end block ───────────────────────────── */

static const char GIT_BLOCK[] =
    "source \"https://rubygems.org\"\n"
    "git \"https://github.com/rails/rails.git\", branch: :main do\n"
    "  gem \"activesupport\"\n"
    "  gem \"activerecord\"\n"
    "end\n";

static void test_git_block(void)
{
    printf("test_git_block:\n");
    struct wow_gemfile gf;
    int rc = parse(GIT_BLOCK, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("dep[0] = activesupport", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "activesupport") == 0);
    check("dep[1] = activerecord", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "activerecord") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: github "org/repo" do ... end block ───────────────────── */

static const char GITHUB_BLOCK[] =
    "source \"https://rubygems.org\"\n"
    "github \"rails/rails\", branch: :main do\n"
    "  gem \"railties\"\n"
    "end\n";

static void test_github_block(void)
{
    printf("test_github_block:\n");
    struct wow_gemfile gf;
    int rc = parse(GITHUB_BLOCK, &gf);
    check("parses OK", rc == 0);
    check("1 dep", gf.n_deps == 1);
    check("dep[0] = railties", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "railties") == 0);
    wow_gemfile_free(&gf);
}

/* ── Test: install_if -> { ... } do ... end ─────────────────────── */

static const char INSTALL_IF_BLOCK[] =
    "source \"https://rubygems.org\"\n"
    "install_if -> { RUBY_PLATFORM =~ /darwin/ } do\n"
    "  gem \"mac-only\"\n"
    "end\n"
    "gem \"universal\"\n";

static void test_install_if(void)
{
    printf("test_install_if:\n");
    struct wow_gemfile gf;
    int rc = parse(INSTALL_IF_BLOCK, &gf);
    check("parses OK", rc == 0);
    check("2 deps", gf.n_deps == 2);
    check("dep[0] = mac-only", gf.n_deps > 0 &&
          strcmp(gf.deps[0].name, "mac-only") == 0);
    check("dep[1] = universal", gf.n_deps > 1 &&
          strcmp(gf.deps[1].name, "universal") == 0);
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

    /* Lexer/parser improvement tests (5 fix patterns) */
    test_group_optional();
    test_group_multi_kw();
    test_array_constraints();
    test_paren_gem();
    test_path_block();
    test_git_block();
    test_github_block();
    test_install_if();

    /* Evaluator tests (Phase 5b) */
    test_eval_if_true();
    test_eval_if_false();
    test_eval_unless();
    test_eval_elsif();
    test_eval_else();
    test_eval_nested_if();
    test_eval_trailing_if_true();
    test_eval_trailing_if_false();
    test_eval_trailing_unless();
    test_eval_env_exists();
    test_eval_env_missing();
    test_eval_env_eq();
    test_eval_ruby_version_cmp();
    test_eval_variable_assign();
    test_eval_variable_env();
    test_eval_gem_version();
    test_eval_and_or();
    test_eval_interleaved();
    test_eval_suppressed_nested();
    test_eval_bang();
    test_version_compare();
    test_eval_gemfile_missing();
    test_eval_interpolation();
    test_eval_realworld();

    printf("\n=== Results: %d passed, %d failed ===\n", n_pass, n_fail);
    return n_fail > 0 ? 1 : 0;
}
