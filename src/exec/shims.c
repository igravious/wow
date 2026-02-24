/*
 * exec/shims.c — Shim generation for Bundler and RubyGems
 *
 * Provides no-op shims that prevent Bundler and RubyGems from
 * interfering with our RUBYLIB-based gem loading.
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "wow/common.h"
#include "wow/exec.h"
#include "wow/util/path.h"

/*
 * Bundler shim content — shadows real bundler/setup.rb
 *
 * When a gem does `require 'bundler/setup'`, this shim intercepts it
 * and defines a minimal no-op Bundler module. Since all gems are
 * already on RUBYLIB, Bundler's setup is unnecessary.
 */
static const char BUNDLER_SHIM_CONTENT[] =
    "# wow shim: shadows real bundler/setup.rb\n"
    "# RUBYLIB already has all gem paths on the load path,\n"
    "# so Bundler's setup is unnecessary.\n"
    "unless defined?(::Bundler) && "
        "::Bundler.respond_to?(:root)\n"
    "  module Bundler\n"
    "    def self.setup(*) self end\n"
    "    def self.require(*) nil end\n"
    "    def self.root() "
        "Pathname.new(Dir.pwd) end\n"
    "    def self.environment() self end\n"
    "    def self.locked_gems() nil end\n"
    "  end\n"
    "end\n";

/*
 * Preload content — stubs Kernel#gem as a no-op
 *
 * Gems call `gem "name", ">= x.y"` to activate via RubyGems.
 * Since we don't have gemspec files, this would fail. Our stub
 * makes these calls no-ops via RUBYOPT=-r< preload >.
 */
static const char PRELOAD_CONTENT[] =
    "module Kernel\n"
    "  def gem(name, *requirements)\n"
    "    true\n"
    "  end\n"
    "  private :gem\n"
    "end\n";

int
wow_ensure_bundler_shim(const char *ruby_prefix)
{
    char shim_path[WOW_OS_PATH_MAX];
    snprintf(shim_path, sizeof(shim_path),
             "%s/lib/wow_shims/bundler/setup.rb", ruby_prefix);

    struct stat st;
    if (stat(shim_path, &st) == 0)
        return 0;  /* Already exists */

    /* Create directory */
    char shim_dir[WOW_OS_PATH_MAX];
    snprintf(shim_dir, sizeof(shim_dir),
             "%s/lib/wow_shims/bundler", ruby_prefix);
    if (wow_mkdirs(shim_dir, 0755) != 0)
        return -1;

    /* Write shim */
    FILE *f = fopen(shim_path, "w");
    if (!f)
        return -1;

    fputs(BUNDLER_SHIM_CONTENT, f);
    fclose(f);
    return 0;
}

int
wow_ensure_gem_preload(const char *ruby_prefix)
{
    char preload_path[WOW_OS_PATH_MAX];
    snprintf(preload_path, sizeof(preload_path),
             "%s/lib/wow_preload.rb", ruby_prefix);

    struct stat st;
    if (stat(preload_path, &st) == 0)
        return 0;  /* Already exists */

    /* Create directory */
    char preload_dir[WOW_OS_PATH_MAX];
    snprintf(preload_dir, sizeof(preload_dir),
             "%s/lib", ruby_prefix);
    if (wow_mkdirs(preload_dir, 0755) != 0)
        return -1;

    /* Write preload */
    FILE *f = fopen(preload_path, "w");
    if (!f)
        return -1;

    fputs(PRELOAD_CONTENT, f);
    fclose(f);
    return 0;
}
