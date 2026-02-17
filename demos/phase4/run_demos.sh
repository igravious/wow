#!/bin/sh
# Phase 4 demo runner — .gem download, inspect, and unpack
#
# Demonstrates wow's gem infrastructure:
# - Download .gem files from rubygems.org with SHA-256 verification
# - List .gem tar contents
# - Parse gemspec YAML metadata (libyaml)
# - Unpack gem data to a directory
#
# Prerequisites: run 'make' from the project root first.
# Usage: cd demos/phase4 && ./run_demos.sh
set -e

cd "$(dirname "$0")"
WOW=../../build/wow.com

if [ ! -x "$WOW" ]; then
    echo "Error: build wow first with 'make' from the project root"
    exit 1
fi

TMPDIR=$(mktemp -d /tmp/wow-phase4-demo-XXXXXX)
trap "rm -rf $TMPDIR" EXIT

# ── Demo 4a: Download a .gem file ──────────────────────────────
echo "================================================================"
echo " Demo 4a: Download .gem file (HTTPS + SHA-256 verification)"
echo "================================================================"
echo ""
echo "Downloading sinatra-4.1.1.gem from rubygems.org..."
echo "  - Fetches registry metadata (URL + SHA-256)"
echo "  - Downloads with progress bar"
echo "  - Verifies SHA-256 digest"
echo "  - Caches to ~/.cache/wow/gems/"
echo ""
echo "\$ wow gem-download sinatra 4.1.1"
$WOW gem-download sinatra 4.1.1
echo ""

# Find the cached gem
GEM_PATH="$HOME/.cache/wow/gems/sinatra-4.1.1.gem"
if [ ! -f "$GEM_PATH" ]; then
    echo "Error: gem not found at $GEM_PATH"
    exit 1
fi

# ── Demo 4b: List .gem contents ────────────────────────────────
echo "================================================================"
echo " Demo 4b: List .gem contents (plain tar parsing)"
echo "================================================================"
echo ""
echo "A .gem is an uncompressed tar with three entries:"
echo "  metadata.gz, data.tar.gz, checksums.yaml.gz"
echo ""
echo "\$ wow gem-list $GEM_PATH"
$WOW gem-list "$GEM_PATH"
echo ""

# ── Demo 4c: Parse gemspec metadata ───────────────────────────
echo "================================================================"
echo " Demo 4c: Parse gemspec metadata (zlib + libyaml)"
echo "================================================================"
echo ""
echo "Extracts metadata.gz from the outer tar, decompresses with zlib,"
echo "and parses the YAML gemspec with libyaml."
echo ""
echo "\$ wow gem-meta $GEM_PATH"
$WOW gem-meta "$GEM_PATH"
echo ""

# ── Demo 4c: Another gem ──────────────────────────────────────
echo "================================================================"
echo " Demo 4c: Parse rack gemspec (shows transitive dep chain)"
echo "================================================================"
echo ""
echo "\$ wow gem-download rack 3.1.14"
$WOW gem-download rack 3.1.14
echo ""
RACK_PATH="$HOME/.cache/wow/gems/rack-3.1.14.gem"
echo "\$ wow gem-meta $RACK_PATH"
$WOW gem-meta "$RACK_PATH"
echo ""

# ── Demo 4d: Unpack gem to directory ──────────────────────────
echo "================================================================"
echo " Demo 4d: Unpack gem to directory"
echo "================================================================"
echo ""
echo "Streams data.tar.gz from outer tar to temp file (no large malloc),"
echo "then extracts the gzip tar to the destination directory."
echo ""
echo "\$ wow gem-unpack $GEM_PATH $TMPDIR/sinatra/"
$WOW gem-unpack "$GEM_PATH" "$TMPDIR/sinatra/"
echo ""
echo "Extracted files:"
ls -la "$TMPDIR/sinatra/" | head -15
echo ""
echo "Sinatra library:"
ls "$TMPDIR/sinatra/lib/"
echo ""

echo "================================================================"
echo " All Phase 4 demos complete."
echo ""
echo " 4a: gem-download — HTTPS download + SHA-256 verification"
echo " 4b: gem-list     — plain tar header parsing"
echo " 4c: gem-meta     — zlib decompression + YAML gemspec parsing"
echo " 4d: gem-unpack   — streaming extraction to destination dir"
echo ""
echo " No curl. No openssl. No ruby. No external dependencies."
echo " One portable binary handles it all."
echo "================================================================"
