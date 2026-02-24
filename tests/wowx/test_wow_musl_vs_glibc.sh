#!/bin/bash
# test_wow_musl_vs_glibc.sh — Does wowx.com work on musl (Alpine) vs glibc (Ubuntu)?
#
# APE (Actually Portable Executable) binaries need either:
#   1. binfmt_misc registered on the host (not inherited by Docker containers), or
#   2. The APE loader (/usr/bin/ape) installed to interpret the polyglot format, or
#   3. The binary converted to a native ELF via cosmocc's `assimilate` tool
#      (strips the APE polyglot header, producing a plain ELF that runs without
#      a loader — but loses cross-platform portability).
#
# This script uses approach (2): mount the host's /usr/bin/ape into each container.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
WOW="$PROJECT_ROOT/build/wow.com"
APE="/usr/bin/ape"

if [ ! -f "$WOW" ]; then
    echo "error: $WOW not found — run make first" >&2
    exit 1
fi

if [ ! -f "$APE" ]; then
    echo "error: $APE not found — install the APE loader first" >&2
    echo "  see: https://justine.lol/cosmopolitan/" >&2
    exit 1
fi

echo "=== Alpine 3.20 (musl) ==="
docker run --rm \
    -v "$WOW:/usr/local/bin/wow:ro" \
    -v "$APE:/usr/bin/ape:ro" \
    alpine:3.20 \
    /usr/bin/ape /usr/local/bin/wow rubies install 3.4.8 \
    2>&1 || true

echo ""
echo "=== Ubuntu 22.04 (glibc) ==="
docker run --rm \
    -v "$WOW:/usr/local/bin/wow:ro" \
    -v "$APE:/usr/bin/ape:ro" \
    ubuntu:22.04 \
    /usr/bin/ape /usr/local/bin/wow rubies install 3.4.8 \
    2>&1 || true
