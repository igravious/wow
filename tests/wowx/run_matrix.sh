#!/bin/bash
# run_matrix.sh — Ruby × gem comparison matrix
#
# Runs the Bundler-vs-wowx comparison across multiple Ruby versions
# in parallel. Each Ruby version gets its own oracle image and its
# own run_comparison.sh process.
#
# Usage:
#   ./tests/wowx/run_matrix.sh                        # default 4 Rubies
#   ./tests/wowx/run_matrix.sh --rubies 3.3,4.0       # specific Rubies
#   ./tests/wowx/run_matrix.sh --gems rubocop,rake     # filter gems
#   ./tests/wowx/run_matrix.sh --skip-tags native      # skip native gems
#   ./tests/wowx/run_matrix.sh -j2                     # parallelism limit
#
# All other flags are passed through to run_comparison.sh.
#
# Requires: docker, curl, jq, build/wowx.com

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RESULTS_DIR="$SCRIPT_DIR/results"

# ── Defaults ──────────────────────────────────────────────────────────

RUBY_VERSIONS="3.2.10,3.3.10,3.4.8,4.0.1"
JOBS=4
PASSTHROUGH_ARGS=()

# ── Options ───────────────────────────────────────────────────────────

while [[ $# -gt 0 ]]; do
    case "$1" in
        --rubies)  RUBY_VERSIONS="$2"; shift 2 ;;
        -j*)
            if [ "${1}" = "-j" ]; then
                JOBS="$2"; shift 2
            else
                JOBS="${1#-j}"; shift
            fi
            ;;
        -h|--help)
            sed -n '2,/^$/{ s/^# //; s/^#$//; p }' "$0"
            exit 0
            ;;
        *)
            PASSTHROUGH_ARGS+=("$1"); shift ;;
    esac
done

# ── Colours ───────────────────────────────────────────────────────────

if [ -t 2 ]; then
    GREEN='\033[32m'  RED='\033[31m'  DIM='\033[2m'  YELLOW='\033[33m'
    BOLD='\033[1m'    RESET='\033[0m'  CYAN='\033[36m'
else
    GREEN='' RED='' DIM='' YELLOW='' BOLD='' RESET='' CYAN=''
fi

log() { printf "%b\n" "$*" >&2; }

# ── Parse Ruby versions ──────────────────────────────────────────────

IFS=',' read -ra RUBIES <<< "$RUBY_VERSIONS"
N_RUBIES=${#RUBIES[@]}

log "${BOLD}wowx comparison matrix${RESET}"
log "${DIM}Rubies: ${RUBIES[*]}${RESET}"
log "${DIM}Parallelism: -j$JOBS${RESET}"
log ""

# ── Pre-build oracle images ──────────────────────────────────────────
#
# Build all Docker images up front so the parallel test runs don't
# compete on docker build. Images are built sequentially (Docker
# daemon is single-threaded for builds anyway).

DOCKERFILE="$SCRIPT_DIR/Dockerfile.oracle"

for rv in "${RUBIES[@]}"; do
    image="wow-oracle:ruby-${rv}"

    DOCKERFILE_SUM=$(sha256sum "$DOCKERFILE" | cut -d' ' -f1)
    NEEDS_BUILD=false

    if ! docker image inspect "$image" &>/dev/null; then
        NEEDS_BUILD=true
    else
        EXISTING_SUM=$(docker inspect --format '{{ index .Config.Labels "wow.dockerfile.sha256" }}' "$image" 2>/dev/null || echo "")
        if [ "$EXISTING_SUM" != "$DOCKERFILE_SUM" ]; then
            docker rmi "$image" >/dev/null 2>&1 || true
            NEEDS_BUILD=true
        fi
    fi

    if $NEEDS_BUILD; then
        log "${CYAN}Building${RESET} $image..."
        DOCKER_BUILDKIT=0 docker build -t "$image" \
            --build-arg "RUBY_VERSION=$rv" \
            --label "wow.dockerfile.sha256=$DOCKERFILE_SUM" \
            -f "$DOCKERFILE" \
            "$SCRIPT_DIR" 2>&1 | grep -v '^DEPRECATED:' | grep -v 'BuildKit' | grep -v 'environment-variable' >&2
        if [ $? -ne 0 ]; then
            log "${RED}Failed to build $image${RESET}"
            exit 1
        fi
        log "${GREEN}Built${RESET} $image"
    else
        log "${DIM}Image $image up to date${RESET}"
    fi
done

log ""

# ── Pre-install Rubies via wowx ──────────────────────────────────────
#
# Like Docker images above, install Rubies sequentially so parallel
# test runs don't race on downloads.  wowx auto-installs on first use,
# but 4 parallel installs fight over the network and disk.

PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
WOWX="${WOWX:-$PROJECT_ROOT/build/wowx.com}"

# Check if --docker-wowx was passed through
DOCKER_WOWX=false
for arg in "${PASSTHROUGH_ARGS[@]}"; do
    [ "$arg" = "--docker-wowx" ] && DOCKER_WOWX=true
done

# Pre-install Rubies sequentially (avoid parallel download races).
# In Docker mode, each run_comparison.sh handles its own pre-install.
if ! $DOCKER_WOWX; then
    for rv in "${RUBIES[@]}"; do
        log "${CYAN}Ensuring${RESET} Ruby $rv..."
        if "$WOWX" --ruby "$rv" rake --version >/dev/null 2>&1; then
            log "${GREEN}Ruby $rv ready${RESET}"
        else
            log "${RED}Failed to ensure Ruby $rv${RESET}"
            exit 1
        fi
    done
else
    log "${DIM}Docker wowx mode — Ruby pre-install handled per container${RESET}"
fi

log ""

# ── Matrix run directory ──────────────────────────────────────────────

MATRIX_ID="matrix-$(date +%Y%m%d-%H%M%S)"
MATRIX_DIR="$RESULTS_DIR/$MATRIX_ID"
mkdir -p "$MATRIX_DIR"
ln -sfn "$MATRIX_ID" "$RESULTS_DIR/latest-matrix"

# ── Launch parallel runs ──────────────────────────────────────────────

declare -A PIDS=()
declare -A LOG_FILES=()

# Set up Ctrl+C handler before launching children
cleanup() {
    log ""
    log "${RED}Interrupted${RESET} — killing child processes..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null
    done
    wait 2>/dev/null
    log "Partial results: $MATRIX_DIR"
    exit 130
}
trap cleanup INT

for rv in "${RUBIES[@]}"; do
    log_file="$MATRIX_DIR/ruby-${rv}.log"
    LOG_FILES["$rv"]="$log_file"

    WOW_MATRIX_RUN=1 "$SCRIPT_DIR/run_comparison.sh" \
        --ruby "$rv" \
        "${PASSTHROUGH_ARGS[@]}" \
        > "$log_file" 2>&1 &
    PIDS["$rv"]=$!

    log "${CYAN}Started${RESET} Ruby $rv (pid ${PIDS[$rv]})"

    # Throttle: wait if we've hit the parallelism limit
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do
        sleep 1
    done
done

# ── Progress ticker while waiting ─────────────────────────────────────

progress_line() {
    local parts=()
    for rv in "${RUBIES[@]}"; do
        local lf="${LOG_FILES[$rv]}"
        local done_n=0 total="?"
        if [ -f "$lf" ]; then
            done_n=$(grep -c '^ok \|^not ok ' "$lf" 2>/dev/null) || done_n=0
            local plan
            plan=$(grep -oP '^1\.\.\K[0-9]+' "$lf" 2>/dev/null) || true
            [ -n "$plan" ] && total="$plan"
        fi
        # Check if process is still alive
        if kill -0 "${PIDS[$rv]}" 2>/dev/null; then
            parts+=("${rv}: ${done_n}/${total}")
        else
            parts+=("${rv}: done")
        fi
    done
    printf "\r  %s" "${parts[*]}" > /dev/stderr
}

# Poll until all children finish
while true; do
    alive=0
    for rv in "${RUBIES[@]}"; do
        kill -0 "${PIDS[$rv]}" 2>/dev/null && alive=$((alive + 1))
    done
    [ "$alive" -eq 0 ] && break
    progress_line
    sleep 2
done
# Clear the progress line
printf "\r%80s\r" "" >&2

# Reap all children
for rv in "${RUBIES[@]}"; do
    wait "${PIDS[$rv]}" 2>/dev/null
done

# ── Collect results ──────────────────────────────────────────────────

TOTAL_PASS=0
TOTAL_FAIL=0
TOTAL_SKIP=0
declare -A RV_PASS=()
declare -A RV_FAIL=()
declare -A RV_SKIP=()
declare -A RV_BUNDLER=()
declare -A RV_WOWX=()

for rv in "${RUBIES[@]}"; do
    # Extract counts from TAP output
    log_file="${LOG_FILES[$rv]}"
    pass_n=$(grep -c '^ok ' "$log_file" 2>/dev/null) || pass_n=0
    fail_n=$(grep -c '^not ok ' "$log_file" 2>/dev/null) || fail_n=0
    skip_n=$(grep -c '# SKIP' "$log_file" 2>/dev/null) || skip_n=0

    RV_PASS["$rv"]=$pass_n
    RV_FAIL["$rv"]=$fail_n
    RV_SKIP["$rv"]=$skip_n

    TOTAL_PASS=$((TOTAL_PASS + pass_n))
    TOTAL_FAIL=$((TOTAL_FAIL + fail_n))
    TOTAL_SKIP=$((TOTAL_SKIP + skip_n))

    # Extract timing totals: sum bundler and wowx-cold milliseconds
    # TAP lines look like: (bundler: 18031ms, wowx-cold: 13359ms)
    b_total=$(grep -oP 'bundler: \K[0-9]+' "$log_file" 2>/dev/null \
              | awk '{s+=$1} END {print s+0}')
    w_total=$(grep -oP 'wowx-cold: \K[0-9]+' "$log_file" 2>/dev/null \
              | awk '{s+=$1} END {print s+0}')
    RV_BUNDLER["$rv"]=$b_total
    RV_WOWX["$rv"]=$w_total
done

# ── Summary ───────────────────────────────────────────────────────────

log ""
log "${BOLD}Matrix summary${RESET} ($N_RUBIES Rubies)"
log "  ${GREEN}$TOTAL_PASS passed${RESET}, ${RED}$TOTAL_FAIL failed${RESET}, ${DIM}$TOTAL_SKIP skipped${RESET}"
log "  Results: $MATRIX_DIR"
log ""

# Per-Ruby status line with speedup
grand_bundler=0
grand_wowx=0
for rv in "${RUBIES[@]}"; do
    p="${RV_PASS[$rv]}"
    f="${RV_FAIL[$rv]}"
    s="${RV_SKIP[$rv]}"
    b="${RV_BUNDLER[$rv]}"
    w="${RV_WOWX[$rv]}"
    grand_bundler=$((grand_bundler + b))
    grand_wowx=$((grand_wowx + w))

    # Compute speedup (bundler_time / wowx_time)
    speedup=""
    if [ "$w" -gt 0 ] && [ "$b" -gt 0 ]; then
        speedup=$(awk "BEGIN { printf \"%.1fx\", $b / $w }")
    fi

    if [ "$f" -eq 0 ]; then
        log "  ${GREEN}Ruby $rv: PASS${RESET} (${p} passed, ${s} skipped)${speedup:+ — ${CYAN}${speedup} vs Bundler${RESET}}"
    else
        log "  ${RED}Ruby $rv: FAIL${RESET} (${p} passed, ${f} failed, ${s} skipped)${speedup:+ — ${CYAN}${speedup} vs Bundler${RESET}}"
    fi
done

# Overall verdict with average speedup
avg_speedup=""
if [ "$grand_wowx" -gt 0 ] && [ "$grand_bundler" -gt 0 ]; then
    avg_speedup=$(awk "BEGIN { printf \"%.1fx\", $grand_bundler / $grand_wowx }")
fi

log ""
if [ $TOTAL_FAIL -eq 0 ]; then
    log "${GREEN}${BOLD}Overall: PASS${RESET}${avg_speedup:+ — ${CYAN}${BOLD}${avg_speedup} vs Bundler (average)${RESET}}"
else
    log "${RED}${BOLD}Overall: FAIL${RESET}${avg_speedup:+ — ${CYAN}${avg_speedup} vs Bundler (average)${RESET}}"
fi
log ""

[ $TOTAL_FAIL -eq 0 ]
