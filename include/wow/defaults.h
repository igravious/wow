/*
 * defaults.h — Centralised default constants for wow
 *
 * This header contains constants used across MULTIPLE source files.
 * Single-use limits belong in their respective modules.
 */

#ifndef WOW_DEFAULTS_H
#define WOW_DEFAULTS_H

#include "wow/version.h"

/* ====================================================================
 * Registry and Source URLs (used across multiple files)
 * ==================================================================== */

/* Default gem registry */
#define WOW_DEFAULT_REGISTRY      "https://rubygems.org"

/* RubyGems API endpoint for gem metadata */
#define WOW_RUBYGEMS_API_URL      "https://rubygems.org/api/v1/gems/"

/* Gem download URL format (name, version) */
#define WOW_GEM_DOWNLOAD_URL_FMT  "https://rubygems.org/downloads/%s-%s.gem"

/* Gem download URL with platform (name, version, platform) */
#define WOW_GEM_PLATFORM_URL_FMT  "https://rubygems.org/downloads/%s-%s-%s.gem"

/* Ruby builder releases URL */
#define WOW_RUBY_BUILDER_URL      "https://github.com/ruby/ruby-builder/releases/download"

/* ====================================================================
 * Default File Names
 * ==================================================================== */

#define WOW_DEFAULT_GEMFILE       "Gemfile"
#define WOW_DEFAULT_LOCKFILE      "Gemfile.lock"
#define WOW_DEFAULT_RUBY_VERSION_FILE ".ruby-version"

/* ====================================================================
 * Cache Directory Structure
 * ==================================================================== */

#define WOW_CACHE_DIR_NAME        "wow"
#define WOWX_CACHE_DIR_NAME       "wowx"
#define WOW_GEM_CACHE_SUBDIR      "gems"

/* ====================================================================
 * Fallback Paths
 * ==================================================================== */

#define WOW_FALLBACK_TMPDIR       "/tmp"

/* ====================================================================
 * Default Versions
 * ==================================================================== */

/* Default Ruby version for Gemfile evaluation */
#define WOW_DEFAULT_RUBY_VERSION  "3.3.0"

/* ====================================================================
 * HTTP Settings
 * ==================================================================== */

#define WOW_HTTP_USER_AGENT       "wow/" WOW_VERSION
#define WOW_HTTP_MAX_RETRIES      3
#define WOW_HTTP_BUFFER_CHUNK     4096

#endif
