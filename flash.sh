#!/bin/bash
# flash.sh
# Usage:  ./flash.sh        → COM7
#         ./flash.sh 3      → COM3

set -e

REPO="samlu22/my-esp32-TFT"
PORT="COM${1:-7}"
LAST_RUN_FILE=".last_run_id"

if [ -z "$GITHUB_TOKEN" ]; then
  echo "ERROR: GITHUB_TOKEN not set. Run: export GITHUB_TOKEN=ghp_..."
  exit 1
fi

echo "Port : $PORT"

# ── get latest run ID from GitHub ─────────────────────────────────────────────
echo "Checking latest run..."
RESPONSE=$(curl -s -L --ssl-no-revoke \
  -H "Authorization: Bearer $GITHUB_TOKEN" \
  -H "Accept: application/vnd.github+json" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  "https://api.github.com/repos/$REPO/actions/runs?per_page=1")

RUN_ID=$(echo "$RESPONSE" | python -c "
import sys,json
data=json.load(sys.stdin)
runs=data.get('workflow_runs',[])
if not runs: sys.exit(1)
print(runs[0]['id'])
") || { 
  echo "ERROR: could not fetch run ID"
  echo "=== RAW RESPONSE ==="
  echo "$RESPONSE"
  echo "===================="
  exit 1
}

CONCLUSION=$(echo "$RESPONSE" | python -c "
import sys,json
data=json.load(sys.stdin)
runs=data.get('workflow_runs',[])
print(runs[0].get('conclusion','unknown')) if runs else print('unknown')
")

echo "Run  : $RUN_ID ($CONCLUSION)"

if [ "$CONCLUSION" != "success" ]; then
  echo "ERROR: latest build did not succeed (status: $CONCLUSION)"
  echo "Check: https://github.com/$REPO/actions"
  exit 1
fi

# ── check if local binary matches latest run ──────────────────────────────────
LOCAL_RUN=""
[ -f "$LAST_RUN_FILE" ] && LOCAL_RUN=$(cat "$LAST_RUN_FILE")

BIN=$(find artifacts/ -name "*.bin" 2>/dev/null | sort | tail -1)

if [ "$LOCAL_RUN" = "$RUN_ID" ] && [ -n "$BIN" ]; then
  echo "Local binary is up to date (run $RUN_ID)"
  echo "Binary: $BIN"
else
  echo "New build detected — downloading run $RUN_ID..."

  ARTIFACT_ID=$(curl -s -L --ssl-no-revoke \
    -H "Authorization: Bearer $GITHUB_TOKEN" \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "https://api.github.com/repos/$REPO/actions/runs/$RUN_ID/artifacts" \
    | python -c "
import sys,json
a=json.load(sys.stdin).get('artifacts',[])
if not a: sys.exit(1)
print(a[0]['id'])
") || { echo "ERROR: no artifacts for this run"; exit 1; }

  rm -rf artifacts/ _fw.zip
  mkdir -p artifacts/

  curl -L --ssl-no-revoke \
    -H "Authorization: Bearer $GITHUB_TOKEN" \
    -H "Accept: application/vnd.github+json" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "https://api.github.com/repos/$REPO/actions/artifacts/$ARTIFACT_ID/zip" \
    -o _fw.zip

  SIZE=$(wc -c < _fw.zip)
  echo "Downloaded: $SIZE bytes"
  if [ "$SIZE" -lt 10000 ]; then
    echo "ERROR: download too small — likely an API error"
    cat _fw.zip
    rm -f _fw.zip
    exit 1
  fi

  unzip -q -o _fw.zip -d artifacts/
  rm -f _fw.zip

  BIN=$(find artifacts/ -name "*.bin" | head -1)
  [ -z "$BIN" ] && { echo "ERROR: no .bin in artifact"; exit 1; }

  echo "$RUN_ID" > "$LAST_RUN_FILE"
  echo "Saved binary: $BIN"
fi

# ── flash ─────────────────────────────────────────────────────────────────────
echo "Erasing flash..."
python -m esptool --chip esp32s3 --port $PORT --baud 921600 \
  erase-flash 2>&1 | grep -E "Erase|success|error|Error"

echo "Flashing..."
python -m esptool --chip esp32s3 --port $PORT --baud 921600 \
  write-flash 0x0 "$BIN" 2>&1 | grep -E "Wrote|Hash|Leaving|Compressed|error|Error"

echo "Done. Replug USB to start firmware."
