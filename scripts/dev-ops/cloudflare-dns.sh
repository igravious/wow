#!/bin/bash
#
# Cloudflare DNS management
#
# Usage:
#   ./cloudflare-dns.sh add <subdomain> <ip>     # Add A record
#   ./cloudflare-dns.sh list                      # List DNS records
#   ./cloudflare-dns.sh delete <record_id>        # Delete a record
#
# Setup:
#   1. Create token: Cloudflare → My Profile → API Tokens → Create Token → "Edit zone DNS"
#   2. Save token:   echo "YOUR_TOKEN" > ~/.config/cloudflare/token && chmod 600 ~/.config/cloudflare/token
#   3. Save zone ID: echo "YOUR_ZONE_ID" > ~/.config/cloudflare/zone_id
#

# Source common utilities
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/../lib/common.sh"

# Configuration
CONFIG_DIR="$HOME/.config/cloudflare"
TOKEN_FILE="$CONFIG_DIR/token"
ZONE_ID_FILE="$CONFIG_DIR/zone_id"
DOMAIN="sahos.org"

# -----------------------------------------------------------------------------
# Setup check
# -----------------------------------------------------------------------------

check_setup() {
  local missing=false

  if [[ ! -f "$TOKEN_FILE" ]]; then
    log_error "Token not found: $TOKEN_FILE"
    echo "  Create one at: https://dash.cloudflare.com/profile/api-tokens"
    echo "  Template: 'Edit zone DNS'"
    echo "  Then run: echo 'YOUR_TOKEN' > $TOKEN_FILE && chmod 600 $TOKEN_FILE"
    missing=true
  fi

  if [[ ! -f "$ZONE_ID_FILE" ]]; then
    log_error "Zone ID not found: $ZONE_ID_FILE"
    echo "  Find it at: Cloudflare Dashboard → $DOMAIN → Overview → right sidebar"
    echo "  Then run: echo 'YOUR_ZONE_ID' > $ZONE_ID_FILE"
    missing=true
  fi

  if [[ "$missing" == true ]]; then
    exit 1
  fi

  CF_TOKEN=$(cat "$TOKEN_FILE")
  CF_ZONE_ID=$(cat "$ZONE_ID_FILE")
}

# -----------------------------------------------------------------------------
# API helpers
# -----------------------------------------------------------------------------

cf_api() {
  local method="$1"
  local endpoint="$2"
  local data="${3:-}"

  local url="https://api.cloudflare.com/client/v4/zones/${CF_ZONE_ID}${endpoint}"

  if [[ -n "$data" ]]; then
    curl -s -X "$method" "$url" \
      -H "Authorization: Bearer $CF_TOKEN" \
      -H "Content-Type: application/json" \
      --data "$data"
  else
    curl -s -X "$method" "$url" \
      -H "Authorization: Bearer $CF_TOKEN" \
      -H "Content-Type: application/json"
  fi
}

# -----------------------------------------------------------------------------
# Commands
# -----------------------------------------------------------------------------

cmd_add() {
  local subdomain="$1"
  local ip="$2"
  local proxied="${3:-true}"

  local full_domain="${subdomain}.${DOMAIN}"

  echo "Adding A record: $full_domain → $ip (proxied: $proxied)"

  local data=$(cat <<EOF
{
  "type": "A",
  "name": "$subdomain",
  "content": "$ip",
  "ttl": 1,
  "proxied": $proxied
}
EOF
)

  local response=$(cf_api POST "/dns_records" "$data")

  if echo "$response" | grep -q '"success":true'; then
    log_ok "Created: $full_domain → $ip"
    echo "$response" | grep -o '"id":"[^"]*"' | head -1
  else
    log_error "Failed to create record"
    echo "$response" | grep -o '"message":"[^"]*"' || echo "$response"
    exit 1
  fi
}

cmd_list() {
  echo "DNS records for $DOMAIN:"
  echo ""

  local response=$(cf_api GET "/dns_records")

  if ! echo "$response" | grep -q '"success":true'; then
    log_error "Failed to fetch records"
    echo "$response"
    exit 1
  fi

  # Parse and display records
  echo "$response" | python3 -c "
import sys, json
data = json.load(sys.stdin)
if data.get('success'):
    records = data.get('result', [])
    for r in sorted(records, key=lambda x: x['name']):
        proxied = '☁️ ' if r.get('proxied') else '   '
        print(f\"{proxied}{r['type']:6} {r['name']:40} → {r['content']:20} (id: {r['id'][:8]}...)\")
else:
    print('Error:', data.get('errors'))
"
}

cmd_delete() {
  local record_id="$1"

  echo "Deleting record: $record_id"

  local response=$(cf_api DELETE "/dns_records/$record_id")

  if echo "$response" | grep -q '"success":true'; then
    log_ok "Deleted record $record_id"
  else
    log_error "Failed to delete record"
    echo "$response" | grep -o '"message":"[^"]*"' || echo "$response"
    exit 1
  fi
}

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------

usage() {
  echo "Cloudflare DNS management for $DOMAIN"
  echo ""
  echo "Usage:"
  echo "  $0 add <subdomain> <ip> [proxied]   Add A record (proxied: true/false, default true)"
  echo "  $0 list                              List all DNS records"
  echo "  $0 delete <record_id>                Delete a record"
  echo ""
  echo "Examples:"
  echo "  $0 add lavie 91.98.229.225           Add lavie.\$DOMAIN → 91.98.229.225 (proxied)"
  echo "  $0 add lavie 91.98.229.225 false     Add without Cloudflare proxy"
  echo "  $0 list                              Show all records"
  echo "  $0 delete abc123def456               Delete record by ID"
}

if [[ $# -lt 1 ]]; then
  usage
  exit 1
fi

check_setup

case "$1" in
  add)
    if [[ $# -lt 3 ]]; then
      log_error "Usage: $0 add <subdomain> <ip> [proxied]"
      exit 1
    fi
    cmd_add "$2" "$3" "${4:-true}"
    ;;
  list)
    cmd_list
    ;;
  delete)
    if [[ $# -lt 2 ]]; then
      log_error "Usage: $0 delete <record_id>"
      exit 1
    fi
    cmd_delete "$2"
    ;;
  *)
    usage
    exit 1
    ;;
esac
