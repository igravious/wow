#!/bin/sh
# Phase 0 demo runner — research proofs-of-concept
#
# These demos prove out the core parsing and detection logic
# that wow needs, each as a standalone program.
#
# Usage: cd demos/phase0 && ./run_demos.sh
set -e

cd "$(dirname "$0")"

echo "Building phase 0 demos..."
make -s
echo ""

# ── Demo 0a: .gem tar parsing ──────────────────────────────────────
echo "================================================================"
echo " Demo 0a: .gem file parsing (ustar tar + gzip decompression)"
echo "================================================================"
echo ""
echo "Downloading sinatra-4.2.1.gem..."
curl -sLO https://rubygems.org/downloads/sinatra-4.2.1.gem
echo ""
./demo_tar.com sinatra-4.2.1.gem
rm -f sinatra-4.2.1.gem
echo ""

# ── Demo 0b: Gemfile.lock parser ──────────────────────────────────
echo "================================================================"
echo " Demo 0b: Gemfile.lock state-machine parser"
echo "================================================================"
echo ""
# Create a sample Gemfile.lock
cat > /tmp/wow-demo-lockfile <<'LOCK'
GEM
  remote: https://rubygems.org/
  specs:
    logger (1.7.0)
    mustermann (3.0.3)
      ruby2_keywords (~> 0.0.1)
    rack (3.1.12)
    rack-protection (4.1.1)
      base64 (>= 0.1.0)
      logger (~> 1.6)
      rack (>= 3.0.0, < 4)
    rack-session (2.1.1)
      base64 (>= 0.1.0)
      rack (>= 3.0.0)
    ruby2_keywords (0.0.5)
    sinatra (4.1.1)
      logger (~> 1.6)
      mustermann (~> 3.0)
      rack (>= 3.0.0, < 4)
      rack-protection (= 4.1.1)
      rack-session (>= 2.0.0, < 3)
      tilt (~> 2.0)
    tilt (2.6.0)
    base64 (0.2.0)

PLATFORMS
  ruby
  x86_64-linux

DEPENDENCIES
  sinatra (~> 4.1)

RUBY VERSION
   ruby 3.4.2p28

BUNDLED WITH
   2.6.2
LOCK
echo "Parsing sample Gemfile.lock..."
echo ""
./demo_lockparse.com /tmp/wow-demo-lockfile
rm -f /tmp/wow-demo-lockfile
echo ""

# ── Demo 0c: Compact index parser ─────────────────────────────────
echo "================================================================"
echo " Demo 0c: Compact index format (rubygems.org /info/{name})"
echo "================================================================"
echo ""
echo "Fetching compact index for 'rack'..."
echo ""
curl -s https://rubygems.org/info/rack | ./demo_compact_index.com | head -40
echo "  ... (truncated)"
echo ""

# ── Demo 0d: Platform detection ───────────────────────────────────
echo "================================================================"
echo " Demo 0d: Host platform detection + ruby-builder URL mapping"
echo "================================================================"
echo ""
./demo_platform.com 3.4.2
echo ""

echo "================================================================"
echo " All phase 0 demos complete."
echo "================================================================"
