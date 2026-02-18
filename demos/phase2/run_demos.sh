#!/bin/sh
# Phase 2 demo runner — native HTTPS + rubygems.org registry
#
# These demos exercise wow's HTTP/TLS client and registry API,
# built with Cosmopolitan's mbedTLS (no curl, no openssl, no deps).
#
# Prerequisites: run 'make' from the project root first.
# Usage: cd demos/phase2 && ./run_demos.sh
set -e

cd "$(dirname "$0")"

echo "Building phase 2 demos..."
make -s
echo ""

# ── Demo 2a: Plain HTTP ───────────────────────────────────────────
echo "================================================================"
echo " Demo 2a: Plain HTTP GET"
echo "================================================================"
echo ""
./demo_https.com http://example.com
echo ""

# ── Demo 2b: HTTPS with TLS ──────────────────────────────────────
echo "================================================================"
echo " Demo 2b: HTTPS GET (mbedTLS handshake + cert validation)"
echo "================================================================"
echo ""
./demo_https.com https://rubygems.org/api/v1/gems/rack.json
echo ""

# ── Demo 2c: Gem info — single gem ───────────────────────────────
echo "================================================================"
echo " Demo 2c: Gem info lookup (HTTPS + JSON parsing)"
echo "================================================================"
echo ""
./demo_gem_info.com sinatra
echo ""

# ── Demo 2c: Gem info — multiple gems ────────────────────────────
echo "================================================================"
echo " Demo 2c: Multiple gem lookups in one invocation"
echo "================================================================"
echo ""
./demo_gem_info.com rails puma nokogiri
echo ""

# ── Demo 2c: Gem info — error handling ───────────────────────────
echo "================================================================"
echo " Demo 2c: Error handling (nonexistent gem)"
echo "================================================================"
echo ""
./demo_gem_info.com this-gem-does-not-exist-99999 2>&1 || true
echo ""

# ── Demo 2d: wow.com subcommands ─────────────────────────────────
echo "================================================================"
echo " Demo 2d: wow.com built-in subcommands"
echo "================================================================"
echo ""
echo "$ wow curl https://rubygems.org/api/v1/gems/puma.json | head -c 200"
../../build/wow.com curl https://rubygems.org/api/v1/gems/puma.json | head -c 200
echo ""
echo "  ... (truncated)"
echo ""
echo "$ wow gem-info sinatra"
../../build/wow.com gem-info sinatra
echo ""

echo "================================================================"
echo " All phase 2 demos complete."
echo " No curl. No openssl. No external dependencies."
echo " One portable binary, native TLS, embedded root CAs."
echo "================================================================"
