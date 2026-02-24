#!/bin/bash
# demo_wowx_gems.sh — wowx smoke test across famous Ruby gems
#
# For each gem: fetches the latest version from rubygems.org, runs
# `wowx <gem> <flag>`, and checks the output contains that version.
#
# This is a real correctness test — not regex pattern matching.
# The expected version is the same number bundler would resolve to.
#
# Usage:
#   ./demos/phase8/demo_wowx_gems.sh              # run all
#   ./demos/phase8/demo_wowx_gems.sh rubocop pry  # run specific gems
#
# Requires: build/wowx.com (run `make` first), curl, jq

set -uo pipefail

WOWX="${WOWX:-build/wowx.com}"
PASS=0
WARN=0
FAIL=0
SKIP=0
TOTAL=0

# ── Colours ───────────────────────────────────────────────────────────

if [ -t 1 ]; then
    GREEN='\033[32m'  RED='\033[31m'  DIM='\033[2m'  YELLOW='\033[33m'
    BOLD='\033[1m'    RESET='\033[0m'
else
    GREEN='' RED='' DIM='' YELLOW='' BOLD='' RESET=''
fi

# ── Gems to test ──────────────────────────────────────────────────────
#
# Each entry: "gem_name|version_flag"
#
#   gem_name     — passed to wowx as argv[1]
#   version_flag — flag to get version output (--version, -v, version)
#
# The expected version is fetched live from rubygems.org.

GEMS=(
    # ── Linters & formatters ──────────────────────────────────────────
    "rubocop|--version"
    "reek|--version"
    "rufo|--version"
    "htmlbeautifier|--version"
    "haml_lint|--version"
    "slim_lint|--version"
    "flay|--version"

    # ── Build & task tools ────────────────────────────────────────────
    "rake|--version"
    "thor|version"
    "foreman|--version"

    # ── Testing ───────────────────────────────────────────────────────
    "rspec-core|--version"

    # ── Documentation ─────────────────────────────────────────────────
    "yard|--version"
    "rdoc|--version"
    "kramdown|--version"

    # ── Debugging & REPL ──────────────────────────────────────────────
    "pry|--version"
    "irb|--version"

    # ── Security ──────────────────────────────────────────────────────
    "brakeman|--version"
    "bundler-audit|--version"

    # ── Type checking & analysis ──────────────────────────────────────
    "rbs|--version"
    "typeprof|--version"
    "ruby-lsp|--version"
    "steep|--version"
    "solargraph|--version"

    # ── Servers ───────────────────────────────────────────────────────
    "rackup|--version"
    "puma|--version"

    # ── Formatters ───────────────────────────────────────────────────
    "syntax_tree|version"

    # ── Top 100 gems with executables ────────────────────────────────
    "diff-lcs|--version"
    "parser|--version"
    "coderay|--version"
    "racc|--version"
    "rails|--version"
    "byebug|--version"
    "cocoapods|--version"
)

# ── Fetch latest versions from rubygems.org ──────────────────────────
#
# Pre-fetch all versions in parallel so the test loop isn't blocked
# by serial HTTP requests.  Uses the JSON API:
#   GET /api/v1/gems/<name>.json → { "version": "1.84.2", ... }

declare -A EXPECTED_VERSIONS

fetch_versions() {
    local gems_to_fetch=("$@")
    local tmpdir
    tmpdir=$(mktemp -d)

    # Launch parallel curl jobs
    for gem_name in "${gems_to_fetch[@]}"; do
        curl -sf "https://rubygems.org/api/v1/gems/${gem_name}.json" \
            -o "$tmpdir/$gem_name.json" &
    done
    wait

    # Parse results
    for gem_name in "${gems_to_fetch[@]}"; do
        local json="$tmpdir/$gem_name.json"
        if [ -f "$json" ]; then
            local ver
            ver=$(jq -r '.version // empty' "$json" 2>/dev/null)
            if [ -n "$ver" ]; then
                EXPECTED_VERSIONS["$gem_name"]="$ver"
            fi
        fi
    done

    rm -rf "$tmpdir"
}

# ── Helpers ───────────────────────────────────────────────────────────

run_gem() {
    local gem_name="$1"
    local version_flag="$2"

    TOTAL=$((TOTAL + 1))

    local expected="${EXPECTED_VERSIONS[$gem_name]:-}"
    if [ -z "$expected" ]; then
        printf "${BOLD}%-20s${RESET} ${YELLOW}SKIP${RESET} (could not fetch version from rubygems.org)\n" "$gem_name"
        SKIP=$((SKIP + 1))
        return
    fi

    # Clean this gem's wowx cache so every run is a cold start
    local cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/wowx"
    rm -rf "$cache_dir/$gem_name"-*/

    printf "${BOLD}%-20s${RESET} " "$gem_name"

    # Time the full invocation (resolve + download + unpack + exec)
    local start
    start=$(date +%s%N 2>/dev/null || python3 -c 'import time; print(int(time.time()*1e9))')

    local output
    output=$("$WOWX" "$gem_name" "$version_flag" 2>/dev/null)
    local rc=$?

    local end
    end=$(date +%s%N 2>/dev/null || python3 -c 'import time; print(int(time.time()*1e9))')

    local elapsed_ms=$(( (end - start) / 1000000 ))

    if [ $rc -ne 0 ]; then
        printf "${RED}FAIL${RESET} (exit %d)  ${DIM}expected %s  %dms${RESET}\n" "$rc" "$expected" "$elapsed_ms"
        FAIL=$((FAIL + 1))
        return
    fi

    # Check output contains the expected version string
    if echo "$output" | grep -qF "$expected"; then
        local version_line
        version_line=$(echo "$output" | grep -F "$expected" | head -1)
        printf "${GREEN}OK${RESET}   %-30s ${DIM}== %s  %dms${RESET}\n" "$version_line" "$expected" "$elapsed_ms"
        PASS=$((PASS + 1))
    else
        local first_line
        first_line=$(echo "$output" | head -1)
        printf "${YELLOW}WARN${RESET} got %-25s ${DIM}expected %s  %dms${RESET}\n" "$first_line" "$expected" "$elapsed_ms"
        WARN=$((WARN + 1))
    fi
}

# ── Banner ────────────────────────────────────────────────────────────

printf "\n"
printf "${BOLD}wowx gem smoke test${RESET} — resolve + download + exec for famous Ruby gems\n"
printf "${DIM}Binary: %s${RESET}\n" "$WOWX"
printf "\n"

if [ ! -x "$WOWX" ]; then
    printf "${RED}error:${RESET} %s not found or not executable\n" "$WOWX"
    printf "Run 'make' first to build wowx.com\n"
    exit 1
fi

# ── Pre-fetch expected versions ──────────────────────────────────────

# Collect gem names to fetch
declare -a gems_list
if [ $# -gt 0 ]; then
    gems_list=("$@")
else
    for entry in "${GEMS[@]}"; do
        IFS='|' read -r name _flag <<< "$entry"
        gems_list+=("$name")
    done
fi

printf "${DIM}Fetching latest versions from rubygems.org...${RESET}"
fetch_versions "${gems_list[@]}"
printf " ${GREEN}%d${RESET} gems\n\n" "${#EXPECTED_VERSIONS[@]}"

# ── Run ───────────────────────────────────────────────────────────────

if [ $# -gt 0 ]; then
    for arg in "$@"; do
        found=0
        for entry in "${GEMS[@]}"; do
            IFS='|' read -r name flag <<< "$entry"
            if [ "$name" = "$arg" ]; then
                run_gem "$name" "$flag"
                found=1
                break
            fi
        done
        if [ $found -eq 0 ]; then
            printf "${BOLD}%-20s${RESET} ${YELLOW}SKIP${RESET} (not in gem list)\n" "$arg"
            SKIP=$((SKIP + 1))
        fi
    done
else
    for entry in "${GEMS[@]}"; do
        IFS='|' read -r name flag <<< "$entry"
        run_gem "$name" "$flag"
    done
fi

# ── Summary ───────────────────────────────────────────────────────────

printf "\n"
printf "${BOLD}Results:${RESET} "
printf "${GREEN}%d passed${RESET}" "$PASS"
if [ $WARN -gt 0 ]; then
    printf ", ${YELLOW}%d warn${RESET}" "$WARN"
fi
if [ $FAIL -gt 0 ]; then
    printf ", ${RED}%d failed${RESET}" "$FAIL"
fi
if [ $SKIP -gt 0 ]; then
    printf ", ${DIM}%d skipped${RESET}" "$SKIP"
fi
printf " / %d total\n\n" "$TOTAL"

[ $FAIL -eq 0 ] && [ $WARN -eq 0 ]
