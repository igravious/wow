#!/bin/sh
# Phase 3 demo runner — parallel HTTPS downloads with pthreads
#
# Demonstrates wow's bounded-concurrency worker pool downloading
# multiple files simultaneously, each with its own TLS session
# and progress bar.
#
# Prerequisites: run 'make' from the project root first.
# Usage: cd demos/phase3 && ./run_demos.sh
set -e

cd "$(dirname "$0")"

echo "Building phase 3 demos..."
make -s
echo ""

# ── Demo 3a: Parallel download — two Ruby tarballs ──────────────
echo "================================================================"
echo " Demo 3a: Parallel Download — Two Ruby Tarballs (2 workers)"
echo "================================================================"
echo ""
echo "Downloading ruby-3.3.6 and ruby-3.4.2 simultaneously..."
echo "Each download has its own TLS session and progress bar."
echo ""
./demo_parallel.com \
  https://github.com/ruby/ruby-builder/releases/download/toolcache/ruby-3.3.6-ubuntu-22.04.tar.gz "ruby-3.3.6-ubuntu-22.04" \
  https://github.com/ruby/ruby-builder/releases/download/toolcache/ruby-3.4.2-ubuntu-22.04.tar.gz "ruby-3.4.2-ubuntu-22.04"
echo ""

# ── Demo 3b: 100 gems — the real show ──────────────────────────
echo "================================================================"
echo " Demo 3b: 100 Popular Ruby Gems (8 concurrent workers)"
echo "================================================================"
echo ""
echo "Downloading the 100 most-depended-upon gems from rubygems.org."
echo "Worker pool: 8 threads, 100 downloads, multi-bar status display."
echo ""
./demo_parallel.com --gems100 -j8
echo ""

echo "================================================================"
echo " All phase 3 demos complete."
echo ""
echo " Demo 3a: fixed-mode multi-bar (one row per download)"
echo " Demo 3b: worker-mode multi-bar (one row per worker + status)"
echo ""
echo " Bounded worker pool, native TLS, embedded root CAs."
echo " No curl. No openssl. No external dependencies."
echo " One portable binary, multiple concurrent HTTPS connections."
echo "================================================================"
