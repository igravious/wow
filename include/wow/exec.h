/*
 * exec.h — Shared execution utilities for wow and wowx
 *
 * Provides Ruby environment setup, shim generation, and gem binary discovery
 * used by both `wow run` and `wowx`.
 */

#ifndef WOW_EXEC_H
#define WOW_EXEC_H

#include <stddef.h>

/*
 * Ensure lib/wow_shims/bundler/setup.rb exists.
 * This shim shadows the real bundler/setup.rb with a no-op Bundler module
 * that prevents Bundler from trying to load a Gemfile.
 *
 * ruby_prefix: The Ruby installation prefix (e.g., /opt/ruby/3.3.0)
 *
 * Returns 0 on success, -1 on error.
 */
int wow_ensure_bundler_shim(const char *ruby_prefix);

/*
 * Ensure lib/wow_preload.rb exists.
 * This stubs Kernel#gem as a no-op since gems are already on RUBYLIB.
 *
 * ruby_prefix: The Ruby installation prefix
 *
 * Returns 0 on success, -1 on error.
 */
int wow_ensure_gem_preload(const char *ruby_prefix);

/*
 * Find a gem binary in an environment directory.
 *
 * env_dir:         Gem environment directory (e.g., vendor/bundle/ruby/3.3.0
 *                  or ~/.cache/wowx/3.3.0/gems-1.0.0)
 * gem_name:        Name of the gem to search (or NULL for any gem)
 * binary_name:     Name of the executable to find
 * exe_path:        Output buffer for found executable path
 * exe_path_sz:     Size of exe_path buffer
 *
 * Search order:
 *   1. If gem_name specified: env_dir/gems/GEM-VER/exe/binary_name
 *   2. If gem_name specified: env_dir/gems/GEM-VER/bin/binary_name
 *   3. Search all gems: env_dir/gems/ANY/exe/binary_name
 *   4. Search all gems: env_dir/gems/ANY/bin/binary_name
 *
 * Also checks .executables markers for gems where the binary name differs
 * from the gem name (e.g., haml_lint gem provides haml-lint binary).
 *
 * Returns 0 if found (exe_path populated), -1 if not found.
 */
int wow_find_gem_binary(const char *env_dir, const char *gem_name,
                        const char *binary_name, char *exe_path,
                        size_t exe_path_sz);

/*
 * Execute a gem binary with proper Ruby environment.
 *
 * ruby_bin:        Path to Ruby executable
 * ruby_api:        Ruby API version (e.g., "3.3.0")
 * env_dir:         Gem environment directory (NULL for direct exec)
 * exe_path:        Path to the gem executable
 * user_argc:       Number of user arguments
 * user_argv:       User arguments array
 *
 * Sets up RUBYLIB with proper ordering (shims → gems → stdlib),
 * creates necessary shims, sets LD_LIBRARY_PATH, then execs the binary.
 *
 * This function does not return on success (execve replaces process).
 * Returns -1 on error (environment setup failed).
 */
int wow_exec_gem_binary(const char *ruby_bin, const char *ruby_api,
                        const char *env_dir, const char *exe_path,
                        int user_argc, char **user_argv);

#endif
