#!/bin/bash
#
# Common utilities for scripts
# Source this file: source "$(dirname "${BASH_SOURCE[0]}")/../lib/common.sh"
#

set -euo pipefail

# -----------------------------------------------------------------------------
# Path Resolution
# -----------------------------------------------------------------------------

# Get the directory where the sourcing script lives
# Usage: SCRIPT_DIR=$(script_dir)
script_dir() {
    cd "$(dirname "${BASH_SOURCE[1]}")" && pwd
}

# Get the project root (parent of scripts/)
# Usage: PROJECT_ROOT=$(project_root)
project_root() {
    local dir
    dir=$(cd "$(dirname "${BASH_SOURCE[1]}")" && pwd)
    cd "${dir}/../.." && pwd
}

# -----------------------------------------------------------------------------
# Colours
# -----------------------------------------------------------------------------

# Detect if stdout is a terminal
if [[ -t 1 ]]; then
    export RED='\033[0;31m'
    export GREEN='\033[0;32m'
    export YELLOW='\033[1;33m'
    export BLUE='\033[0;34m'
    export CYAN='\033[0;36m'
    export NC='\033[0m' # No Colour
else
    export RED=''
    export GREEN=''
    export YELLOW=''
    export BLUE=''
    export CYAN=''
    export NC=''
fi

# -----------------------------------------------------------------------------
# Logging
# -----------------------------------------------------------------------------

log_info()  { echo -e "${BLUE}[INFO]${NC} $*"; }
log_ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }
log_debug() { echo -e "${CYAN}[DEBUG]${NC} $*"; }

# -----------------------------------------------------------------------------
# Error Handling
# -----------------------------------------------------------------------------

# Show error and exit
die() {
    log_error "$*"
    exit 1
}

# Check if command exists
require_command() {
    if ! command -v "$1" &> /dev/null; then
        die "Required command not found: $1"
    fi
}

# -----------------------------------------------------------------------------
# User Confirmation
# -----------------------------------------------------------------------------

# Ask for confirmation
confirm() {
    local prompt="${1:-Continue?}"
    read -rp "$prompt [y/N] " response
    [[ "$response" =~ ^[Yy]$ ]]
}

# Confirm or exit
confirm_or_exit() {
    local prompt="${1:-Continue?}"
    if ! confirm "$prompt"; then
        log_info "Aborted."
        exit 0
    fi
}
