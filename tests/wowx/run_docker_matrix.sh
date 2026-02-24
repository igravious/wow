#!/bin/bash
# run_docker_matrix.sh — Docker-based Ruby × gem comparison matrix
#
# Like run_matrix.sh, but runs wowx INSIDE Docker to level the playing
# field with the Bundler oracle (same network, CPU, filesystem).
#
# Usage:
#   ./tests/wowx/run_docker_matrix.sh                      # default 4 Rubies
#   ./tests/wowx/run_docker_matrix.sh --gems rubocop       # filter gems
#   ./tests/wowx/run_docker_matrix.sh --rubies 3.3,4.0     # specific Rubies
#   ./tests/wowx/run_docker_matrix.sh -j2                   # parallelism limit
#
# All other flags are passed through to run_comparison.sh.
#
# Requires: docker, curl, jq, build/wowx.com

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

exec "$SCRIPT_DIR/run_matrix.sh" --docker-wowx "$@"
