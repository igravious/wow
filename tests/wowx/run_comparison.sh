#!/bin/bash
# run_comparison.sh — Bundler-vs-wowx comparison test harness
#
# Runs each gem through both Bundler (Docker oracle) and wowx, comparing
# stdout byte-for-byte. Bundler is the oracle — if output diverges, wowx
# is wrong.
#
# Output format: TAP (Test Anything Protocol) for CI integration.
#
# Usage:
#   ./tests/wowx/run_comparison.sh                    # all gems, latest version
#   ./tests/wowx/run_comparison.sh --gems rubocop     # specific gem(s)
#   ./tests/wowx/run_comparison.sh --gems rubocop,pry # comma-separated
#   ./tests/wowx/run_comparison.sh --versions          # use version_matrix.tsv
#   ./tests/wowx/run_comparison.sh --warm              # also test warm cache
#   ./tests/wowx/run_comparison.sh --normalise         # strip trailing whitespace
#   ./tests/wowx/run_comparison.sh --tags native       # filter by tag
#   ./tests/wowx/run_comparison.sh --skip-tags native  # exclude by tag
#   ./tests/wowx/run_comparison.sh --ruby 3.2          # oracle Ruby version
#   ./tests/wowx/run_comparison.sh --timeout 60         # wowx timeout (seconds)
#   ./tests/wowx/run_comparison.sh --bundler-timeout 600  # bundler timeout (seconds)
#
# Requires: docker, curl, jq, build/wowx.com

set -uo pipefail

# ── Paths ─────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
MANIFEST="$SCRIPT_DIR/gem_manifest.tsv"
VERSION_MATRIX="$SCRIPT_DIR/version_matrix.tsv"
RESULTS_DIR="$SCRIPT_DIR/results"
WOWX="${WOWX:-$PROJECT_ROOT/build/wowx.com}"
GEM_CACHE="${WOW_ORACLE_CACHE:-$HOME/.cache/wow-oracle/gems}"
RUBY_VERSION="${WOW_ORACLE_RUBY:-4.0.1}"
ORACLE_IMAGE="wow-oracle:ruby-${RUBY_VERSION}"
BUNDLER_TIMEOUT=300  # 5 min — bundle install is slow (that's why wow exists)
WOWX_TIMEOUT=120     # 2 min — cold installs with native extensions need time

# ── Options ───────────────────────────────────────────────────────────

FILTER_GEMS=""
USE_MATRIX=false
TEST_WARM=false
NORMALISE=false
FILTER_TAGS=""
SKIP_TAGS=""
DOCKER_WOWX=false
WOWX_IMAGE="wow-wowx"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --gems)       FILTER_GEMS="$2"; shift 2 ;;
        --versions)   USE_MATRIX=true; shift ;;
        --warm)       TEST_WARM=true; shift ;;
        --normalise)  NORMALISE=true; shift ;;
        --tags)       FILTER_TAGS="$2"; shift 2 ;;
        --skip-tags)  SKIP_TAGS="$2"; shift 2 ;;
        --ruby)       RUBY_VERSION="$2"; ORACLE_IMAGE="wow-oracle:ruby-${RUBY_VERSION}"; shift 2 ;;
        --timeout)    WOWX_TIMEOUT="$2"; shift 2 ;;
        --bundler-timeout) BUNDLER_TIMEOUT="$2"; shift 2 ;;
        --docker-wowx) DOCKER_WOWX=true; shift ;;
        -h|--help)
            sed -n '2,/^$/{ s/^# //; s/^#$//; p }' "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            exit 1
            ;;
    esac
done

# ── Colours (stderr only — TAP goes to stdout) ───────────────────────

if [ -t 2 ]; then
    GREEN='\033[32m'  RED='\033[31m'  DIM='\033[2m'  YELLOW='\033[33m'
    BOLD='\033[1m'    RESET='\033[0m'
else
    GREEN='' RED='' DIM='' YELLOW='' BOLD='' RESET=''
fi

log()  { printf "%b\n" "$*" >&2; }
info() { log "${DIM}# $*${RESET}"; }
pass() { log "${GREEN}$*${RESET}"; }
fail() { log "${RED}$*${RESET}"; }
warn() { log "${YELLOW}$*${RESET}"; }

# ── Ctrl+C handler ────────────────────────────────────────────────────

interrupted() {
    echo ""
    echo "Bail out! Interrupted by user (Ctrl+C)"
    log ""
    log "${RED}Interrupted${RESET} after $TEST_NUM / $TOTAL_TESTS tests"
    log "  ${GREEN}${PASS_COUNT} passed${RESET}, ${RED}${FAIL_COUNT} failed${RESET}, ${SKIP_COUNT} skipped"
    if [ -n "${RUN_DIR:-}" ]; then
        log "  Partial results: $RUN_DIR"
    fi
    exit 130
}
trap interrupted INT

# ── Preflight checks ─────────────────────────────────────────────────

if ! $DOCKER_WOWX && [ ! -x "$WOWX" ]; then
    log "${RED}error:${RESET} $WOWX not found or not executable"
    log "Run 'make' first to build wowx.com"
    exit 1
fi

if ! command -v docker &>/dev/null; then
    log "${RED}error:${RESET} docker not found — required for Bundler oracle"
    exit 1
fi

if [ ! -f "$MANIFEST" ]; then
    log "${RED}error:${RESET} $MANIFEST not found"
    exit 1
fi

# ── Docker wowx image ───────────────────────────────────────────────
#
# When --docker-wowx is set, build/ensure the wowx Docker image and
# pre-install the requested Ruby inside a persistent cache volume.

WOWX_DOCKER_CACHE="${WOW_WOWX_DOCKER_CACHE:-$HOME/.cache/wow-wowx-docker}"

if $DOCKER_WOWX; then
    DOCKERFILE_WOWX="$SCRIPT_DIR/Dockerfile.wowx"
    if [ ! -f "$DOCKERFILE_WOWX" ]; then
        log "${RED}error:${RESET} $DOCKERFILE_WOWX not found"
        exit 1
    fi

    WOWX_SUM=$(sha256sum "$DOCKERFILE_WOWX" "$WOWX" 2>/dev/null | sha256sum | cut -d' ' -f1)
    WOWX_NEEDS_BUILD=false

    if ! docker image inspect "$WOWX_IMAGE" &>/dev/null; then
        WOWX_NEEDS_BUILD=true
    else
        EXISTING_WOWX_SUM=$(docker inspect --format '{{ index .Config.Labels "wow.wowx.sha256" }}' "$WOWX_IMAGE" 2>/dev/null || echo "")
        if [ "$EXISTING_WOWX_SUM" != "$WOWX_SUM" ]; then
            info "wowx binary or Dockerfile changed — rebuilding wowx image..."
            docker rmi "$WOWX_IMAGE" >/dev/null 2>&1 || true
            WOWX_NEEDS_BUILD=true
        fi
    fi

    if $WOWX_NEEDS_BUILD; then
        info "Building wowx Docker image..."
        # Use a minimal build context (just the binary + Dockerfile)
        # to avoid sending the entire repo to Docker.
        wowx_ctx=$(mktemp -d)
        cp "$WOWX" "$wowx_ctx/wowx.com"
        cp "$DOCKERFILE_WOWX" "$wowx_ctx/Dockerfile"
        DOCKER_BUILDKIT=0 docker build -t "$WOWX_IMAGE" \
            --build-arg "WOWX=wowx.com" \
            --label "wow.wowx.sha256=$WOWX_SUM" \
            "$wowx_ctx" 2>&1 | grep -v '^DEPRECATED:' | grep -v 'BuildKit' | grep -v 'environment-variable' >&2
        rm -rf "$wowx_ctx"
    else
        info "Image $WOWX_IMAGE up to date"
    fi

    # Pre-install Ruby inside Docker (cached across runs).
    # Mount a persistent volume as HOME so both Rubies (~/.local/share/wow/)
    # and gem cache (~/.cache/wowx/) persist between container runs.
    mkdir -p "$WOWX_DOCKER_CACHE"
    info "Ensuring Ruby $RUBY_VERSION is available in wowx Docker..."
    docker run --rm \
        -v "$WOWX_DOCKER_CACHE:/wowx-home" \
        -e HOME=/wowx-home \
        "$WOWX_IMAGE" \
        wowx --ruby "$RUBY_VERSION" rake --version >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        log "${RED}error:${RESET} wowx Docker cannot use Ruby $RUBY_VERSION"
        exit 1
    fi
else
    # ── Ensure wowx has the requested Ruby (host mode) ──────────────
    #
    # Trigger auto-install outside the timed test loop.  The first wowx
    # invocation for a new Ruby downloads ~35 MiB and extracts it (~45 s),
    # which would blow the WOWX_TIMEOUT otherwise.

    info "Ensuring Ruby $RUBY_VERSION is available to wowx..."
    "$WOWX" --ruby "$RUBY_VERSION" rake --version >/dev/null 2>&1
    if [ $? -ne 0 ]; then
        log "${RED}error:${RESET} wowx cannot use Ruby $RUBY_VERSION (auto-install failed?)"
        log "Try: $WOWX --ruby $RUBY_VERSION rake --version"
        exit 1
    fi
fi

# ── Ensure oracle image is up to date ─────────────────────────────────
#
# Rebuild when: image doesn't exist, or Dockerfile has changed since
# the last build. We stash a checksum in a label on the image itself.

DOCKERFILE="$SCRIPT_DIR/Dockerfile.oracle"
DOCKERFILE_SUM=$(sha256sum "$DOCKERFILE" | cut -d' ' -f1)
NEEDS_BUILD=false

if ! docker image inspect "$ORACLE_IMAGE" &>/dev/null; then
    NEEDS_BUILD=true
else
    EXISTING_SUM=$(docker inspect --format '{{ index .Config.Labels "wow.dockerfile.sha256" }}' "$ORACLE_IMAGE" 2>/dev/null || echo "")
    if [ "$EXISTING_SUM" != "$DOCKERFILE_SUM" ]; then
        info "Dockerfile changed — rebuilding oracle image $ORACLE_IMAGE..."
        docker rmi "$ORACLE_IMAGE" >/dev/null 2>&1 || true
        NEEDS_BUILD=true
    fi
fi

if $NEEDS_BUILD; then
    info "Building oracle image $ORACLE_IMAGE..."
    DOCKER_BUILDKIT=0 docker build -t "$ORACLE_IMAGE" \
        --build-arg "RUBY_VERSION=$RUBY_VERSION" \
        --label "wow.dockerfile.sha256=$DOCKERFILE_SUM" \
        -f "$DOCKERFILE" \
        "$SCRIPT_DIR" 2>&1 | grep -v '^DEPRECATED:' | grep -v 'BuildKit' | grep -v 'environment-variable' >&2
    if [ $? -ne 0 ]; then
        fail "Failed to build oracle image"
        exit 1
    fi
fi

# ── Ensure gem cache directory exists ─────────────────────────────────

mkdir -p "$GEM_CACHE"

# ── Parse manifest ────────────────────────────────────────────────────

declare -a GEMS=()       # "gem\texe\tflag\ttags" entries
declare -A GEM_EXE=()    # gem → exe
declare -A GEM_FLAG=()   # gem → flag
declare -A GEM_TAGS=()   # gem → tags

while IFS=$'\t' read -r gem exe flag tags; do
    # Skip comments and blank lines
    [[ "$gem" =~ ^#.*$ ]] && continue
    [[ -z "$gem" ]] && continue

    # Apply tag filters
    if [ -n "$FILTER_TAGS" ]; then
        local_match=false
        IFS=',' read -ra want_tags <<< "$FILTER_TAGS"
        for wt in "${want_tags[@]}"; do
            if [[ ",$tags," == *",$wt,"* ]]; then
                local_match=true
                break
            fi
        done
        $local_match || continue
    fi

    if [ -n "$SKIP_TAGS" ]; then
        local_skip=false
        IFS=',' read -ra skip_tags <<< "$SKIP_TAGS"
        for st in "${skip_tags[@]}"; do
            if [[ ",$tags," == *",$st,"* ]]; then
                local_skip=true
                break
            fi
        done
        $local_skip && continue
    fi

    # Apply gem name filter
    if [ -n "$FILTER_GEMS" ]; then
        IFS=',' read -ra wanted <<< "$FILTER_GEMS"
        found=false
        for w in "${wanted[@]}"; do
            [ "$w" = "$gem" ] && found=true && break
        done
        $found || continue
    fi

    GEMS+=("${gem}")
    GEM_EXE["$gem"]="$exe"
    GEM_FLAG["$gem"]="$flag"
    GEM_TAGS["$gem"]="${tags:-}"
done < "$MANIFEST"

if [ ${#GEMS[@]} -eq 0 ]; then
    log "${RED}error:${RESET} no gems matched filters"
    exit 1
fi

# ── Build version list ────────────────────────────────────────────────
#
# If --versions and version_matrix.tsv exists, use it.
# Otherwise, fetch latest version from rubygems.org for each gem.

declare -A GEM_VERSIONS=()  # gem → "ver1 ver2 ver3"

if $USE_MATRIX && [ -f "$VERSION_MATRIX" ]; then
    info "Reading version matrix from $VERSION_MATRIX"
    while IFS=$'\t' read -r gem ver; do
        [[ "$gem" =~ ^#.*$ ]] && continue
        [[ -z "$gem" ]] && continue
        # Only include gems that passed our filters
        [[ -v "GEM_EXE[$gem]" ]] || continue
        GEM_VERSIONS["$gem"]="${GEM_VERSIONS[$gem]:-} $ver"
    done < "$VERSION_MATRIX"
fi

# For gems without matrix entries, fetch latest from rubygems.org
gems_to_fetch=()
for gem in "${GEMS[@]}"; do
    if [[ ! -v "GEM_VERSIONS[$gem]" ]] || [ -z "${GEM_VERSIONS[$gem]}" ]; then
        gems_to_fetch+=("$gem")
    fi
done

if [ ${#gems_to_fetch[@]} -gt 0 ]; then
    info "Fetching latest versions from rubygems.org for ${#gems_to_fetch[@]} gems..."
    tmpdir=$(mktemp -d)

    for gem in "${gems_to_fetch[@]}"; do
        curl -sf "https://rubygems.org/api/v1/gems/${gem}.json" \
            -o "$tmpdir/$gem.json" &
    done
    wait

    for gem in "${gems_to_fetch[@]}"; do
        if [ -f "$tmpdir/$gem.json" ]; then
            ver=$(jq -r '.version // empty' "$tmpdir/$gem.json" 2>/dev/null)
            if [ -n "$ver" ]; then
                GEM_VERSIONS["$gem"]="$ver"
            fi
        fi
    done
    rm -rf "$tmpdir"
fi

# ── Results directory ─────────────────────────────────────────────────

RUN_ID="$(date +%Y%m%d-%H%M%S)-ruby-${RUBY_VERSION}"
RUN_DIR="$RESULTS_DIR/$RUN_ID"
mkdir -p "$RUN_DIR"
# Symlink latest → this run (skip when called from matrix to avoid races)
if [ -z "${WOW_MATRIX_RUN:-}" ]; then
    ln -sfn "$RUN_ID" "$RESULTS_DIR/latest"
fi

# ── Test runner ───────────────────────────────────────────────────────

TEST_NUM=0
PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

# Count total tests for TAP plan
TOTAL_TESTS=0
for gem in "${GEMS[@]}"; do
    versions="${GEM_VERSIONS[$gem]:-}"
    if [ -z "$versions" ]; then
        TOTAL_TESTS=$((TOTAL_TESTS + 1))  # will be a skip
    else
        for _ver in $versions; do
            TOTAL_TESTS=$((TOTAL_TESTS + 1))
            $TEST_WARM && TOTAL_TESTS=$((TOTAL_TESTS + 1))
        done
    fi
done

# TAP header
echo "TAP (https://testanything.org/tap-version-13-specification.html) version 13"
echo "# Ruby: $RUBY_VERSION (oracle: $ORACLE_IMAGE, wowx: --ruby $RUBY_VERSION)"
echo "1..$TOTAL_TESTS"
info "Ruby $RUBY_VERSION — oracle: $ORACLE_IMAGE, wowx: --ruby $RUBY_VERSION"

run_bundler_oracle() {
    local gem="$1"
    local ver="$2"
    local exe="$3"
    local flag="$4"
    local out_file="$5"
    local rc_file="$6"

    # Write a self-contained script to avoid shell quoting issues.
    # The script is passed via -e env vars and executed with sh -c.
    # Docker's own exit code reflects `bundle exec` exit code.
    timeout "$BUNDLER_TIMEOUT" docker run --rm \
        -v "$GEM_CACHE:/gem-cache" \
        -e "T_GEM=$gem" \
        -e "T_VER=$ver" \
        -e "T_EXE=$exe" \
        -e "T_FLAG=$flag" \
        "$ORACLE_IMAGE" sh -c '
            cd /app
            cat > Gemfile <<GEMFILE
source "https://rubygems.org"
gem "$T_GEM"
GEMFILE
            bundle config set --local path /gem-cache 2>/dev/null
            bundle install --quiet 2>/dev/null
            exec bundle exec $T_EXE $T_FLAG
        ' >"$out_file" 2>/dev/null
    echo $? > "$rc_file"
}

run_wowx() {
    local gem="$1"
    local flag="$2"
    local out_file="$3"
    local rc_file="$4"
    local clean_cache="$5"

    if $DOCKER_WOWX; then
        # Run wowx inside Docker — same conditions as Bundler oracle.
        # Mount persistent HOME so Rubies + gem cache persist.
        local ruby_api="${RUBY_VERSION%.*}.0"
        local clean_cmd=""
        $clean_cache && clean_cmd="rm -rf /wowx-home/.cache/wowx/${ruby_api}/${gem}-*/ &&"

        timeout "$WOWX_TIMEOUT" docker run --rm \
            -v "$WOWX_DOCKER_CACHE:/wowx-home" \
            -e HOME=/wowx-home \
            -e "T_GEM=$gem" \
            -e "T_FLAG=$flag" \
            -e "T_RUBY=$RUBY_VERSION" \
            "$WOWX_IMAGE" sh -c "
                ${clean_cmd}
                exec wowx --ruby \$T_RUBY \$T_GEM \$T_FLAG
            " >"$out_file" 2>/dev/null
        echo $? > "$rc_file"
    else
        # Run wowx on host
        if $clean_cache; then
            local ruby_api="${RUBY_VERSION%.*}.0"
            local cache_dir="${XDG_CACHE_HOME:-$HOME/.cache}/wowx/${ruby_api}"
            rm -rf "$cache_dir/$gem"-*/
        fi

        timeout "$WOWX_TIMEOUT" "$WOWX" --ruby "$RUBY_VERSION" "$gem" "$flag" \
            >"$out_file" 2>/dev/null
        echo $? > "$rc_file"
    fi
}

compare_outputs() {
    local test_id="$1"
    local label="$2"
    local bundler_out="$3"
    local wowx_out="$4"
    local bundler_rc="$5"
    local wowx_rc="$6"
    local diff_file="$7"

    TEST_NUM=$((TEST_NUM + 1))

    # Normalise if requested
    local cmp_bundler="$bundler_out"
    local cmp_wowx="$wowx_out"
    if $NORMALISE; then
        cmp_bundler="${bundler_out}.norm"
        cmp_wowx="${wowx_out}.norm"
        sed 's/[[:space:]]*$//' "$bundler_out" > "$cmp_bundler"
        sed 's/[[:space:]]*$//' "$wowx_out" > "$cmp_wowx"
    fi

    # Compare exit codes
    local b_rc w_rc
    b_rc=$(cat "$bundler_rc" 2>/dev/null)
    w_rc=$(cat "$wowx_rc" 2>/dev/null)
    b_rc="${b_rc:-999}"
    w_rc="${w_rc:-999}"

    # Compare stdout
    if diff -u "$cmp_bundler" "$cmp_wowx" > "$diff_file" 2>/dev/null && [ "$b_rc" = "$w_rc" ]; then
        PASS_COUNT=$((PASS_COUNT + 1))
        echo "ok $TEST_NUM - $label"
        return 0
    else
        FAIL_COUNT=$((FAIL_COUNT + 1))
        echo "not ok $TEST_NUM - $label"

        # TAP diagnostics (YAML block)
        echo "  ---"
        if [ "$b_rc" != "$w_rc" ]; then
            echo "  exit_code_bundler: $b_rc"
            echo "  exit_code_wowx: $w_rc"
        fi
        if [ -s "$diff_file" ]; then
            echo "  diff: |"
            sed 's/^/    /' "$diff_file"
        fi
        echo "  ..."

        return 1
    fi
}

# ── Main loop ─────────────────────────────────────────────────────────

for gem in "${GEMS[@]}"; do
    exe="${GEM_EXE[$gem]}"
    flag="${GEM_FLAG[$gem]}"
    tags="${GEM_TAGS[$gem]:-}"
    versions="${GEM_VERSIONS[$gem]:-}"

    if [ -z "$versions" ]; then
        TEST_NUM=$((TEST_NUM + 1))
        SKIP_COUNT=$((SKIP_COUNT + 1))
        echo "ok $TEST_NUM - $gem # SKIP could not fetch version from rubygems.org"
        warn "  skip $TEST_NUM - $gem (no version available)"
        continue
    fi

    for ver in $versions; do
        test_id="${gem}-${ver}"
        test_dir="$RUN_DIR/$test_id"
        mkdir -p "$test_dir"

        info "Testing $((TEST_NUM + 1)) (of $TOTAL_TESTS) [ruby $RUBY_VERSION]: $gem $ver ($exe $flag)..."

        # ── Bundler oracle ────────────────────────────────────────
        bundler_start=$(date +%s%N 2>/dev/null || echo 0)
        run_bundler_oracle "$gem" "$ver" "$exe" "$flag" \
            "$test_dir/bundler.out" "$test_dir/bundler.rc"
        bundler_end=$(date +%s%N 2>/dev/null || echo 0)
        bundler_ms=$(( (bundler_end - bundler_start) / 1000000 ))

        # ── wowx (cold cache) ────────────────────────────────────
        wowx_start=$(date +%s%N 2>/dev/null || echo 0)
        run_wowx "$gem" "$flag" \
            "$test_dir/wowx-cold.out" "$test_dir/wowx-cold.rc" true
        wowx_end=$(date +%s%N 2>/dev/null || echo 0)
        wowx_cold_ms=$(( (wowx_end - wowx_start) / 1000000 ))

        # ── Compare (cold) ────────────────────────────────────────
        label="$gem $ver (bundler: ${bundler_ms}ms, wowx-cold: ${wowx_cold_ms}ms)"
        compare_outputs "$test_id" "$label" \
            "$test_dir/bundler.out" "$test_dir/wowx-cold.out" \
            "$test_dir/bundler.rc" "$test_dir/wowx-cold.rc" \
            "$test_dir/cold.diff"

        # ── wowx (warm cache) ────────────────────────────────────
        if $TEST_WARM; then
            wowx_start=$(date +%s%N 2>/dev/null || echo 0)
            run_wowx "$gem" "$flag" \
                "$test_dir/wowx-warm.out" "$test_dir/wowx-warm.rc" false
            wowx_end=$(date +%s%N 2>/dev/null || echo 0)
            wowx_warm_ms=$(( (wowx_end - wowx_start) / 1000000 ))

            label="$gem $ver warm (wowx-warm: ${wowx_warm_ms}ms)"
            compare_outputs "${test_id}-warm" "$label" \
                "$test_dir/bundler.out" "$test_dir/wowx-warm.out" \
                "$test_dir/bundler.rc" "$test_dir/wowx-warm.rc" \
                "$test_dir/warm.diff"
        fi
    done
done

# ── Summary ───────────────────────────────────────────────────────────

echo ""
info "Ruby $RUBY_VERSION — Results: ${PASS_COUNT} passed, ${FAIL_COUNT} failed, ${SKIP_COUNT} skipped / $TEST_NUM total"
info "Output:  $RUN_DIR"

if [ $FAIL_COUNT -gt 0 ]; then
    fail "FAILED: $FAIL_COUNT test(s) diverged from Bundler oracle"
    exit 1
else
    pass "ALL PASSED: wowx output matches Bundler byte-for-byte"
    exit 0
fi
