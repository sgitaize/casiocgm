#!/usr/bin/env bash
# Deploy config/ to FTP remote path from .ftpconfig
# Usage: ./deploy_ftp.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG="$SCRIPT_DIR/.ftpconfig"

if [[ ! -f "$CONFIG" ]]; then
  echo "ERROR: .ftpconfig not found at $CONFIG" >&2
  exit 1
fi

if ! command -v python3 &>/dev/null; then
  echo "ERROR: python3 not found. Install Python 3 to parse .ftpconfig" >&2
  exit 1
fi

read_config() {
  local key="$1"
  python3 - "$CONFIG" "$key" <<'PY'
import json
import sys

config_path = sys.argv[1]
key = sys.argv[2]

with open(config_path, "r", encoding="utf-8") as f:
    data = json.load(f)

value = data.get(key)
if value is None:
    sys.exit(2)

print(value)
PY
}

if ! FTP_HOST="$(read_config host)"; then
  echo "ERROR: missing 'host' in .ftpconfig" >&2
  exit 1
fi
if ! FTP_USER="$(read_config user)"; then
  echo "ERROR: missing 'user' in .ftpconfig" >&2
  exit 1
fi
if ! FTP_PASS="$(read_config password)"; then
  echo "ERROR: missing 'password' in .ftpconfig" >&2
  exit 1
fi
if ! REMOTE_DIR="$(read_config remote)"; then
  echo "ERROR: missing 'remote' in .ftpconfig" >&2
  exit 1
fi

LOCAL_DIR="$SCRIPT_DIR/config"

if ! command -v lftp &>/dev/null; then
  echo "lftp not found. Install with: brew install lftp"
  exit 1
fi

echo "Deploying $LOCAL_DIR -> ftp://$FTP_HOST$REMOTE_DIR ..."

lftp -u "$FTP_USER","$FTP_PASS" "$FTP_HOST" <<EOF
set ssl:verify-certificate no
mirror --reverse --delete --verbose "$LOCAL_DIR" "$REMOTE_DIR"
bye
EOF

echo "Done."
