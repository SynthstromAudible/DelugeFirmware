#!/usr/bin/env bash
set -euo pipefail

# Run from website/ after building the site.
SITE_BASE_PATH=${SITE_BASE_PATH:-}
preview_log=$(mktemp)
# --ignore-lock keeps the server in the foreground (Astro 7 backgrounds it when
# it detects an AI agent) so this script owns, and later kills, the process.
node ./node_modules/astro/bin/astro.mjs preview --host 127.0.0.1 --port 4321 --ignore-lock > "$preview_log" 2>&1 &
preview_pid=$!
trap 'kill "$preview_pid" 2>/dev/null || true; rm -f "$preview_log"' EXIT

preview_url="http://127.0.0.1:4321${SITE_BASE_PATH}/"
ready=false
for attempt in {1..60}; do
  if curl --fail --silent --max-time 2 "$preview_url" > /dev/null; then
    ready=true
    break
  fi
  if ! kill -0 "$preview_pid" 2>/dev/null; then
    cat "$preview_log"
    exit 1
  fi
  sleep 1
done
if [[ "$ready" != true ]]; then
  cat "$preview_log"
  echo "Preview server did not become ready within 60 seconds" >&2
  exit 1
fi

browser_executable=$(node --input-type=module -e 'import { chromium } from "playwright"; console.log(chromium.executablePath())')
pnpm exec starlight-to-pdf "$preview_url" \
  --browser-executable "$browser_executable" \
  --path ./dist/downloads \
  --filename delugecommunity.com \
  --contents-links internal \
  --exclude "${SITE_BASE_PATH}/downloads" \
  --exclude "${SITE_BASE_PATH}/reference/deluge_companion" \
  --preceding-html src/assets/starlight2pdf/templates/cover.html \
  --header src/assets/starlight2pdf/templates/header-template.html \
  --footer src/assets/starlight2pdf/templates/footer-template.html \
  --print-bg \
  --pdf-outline \
  --margins '0 0 0 0' \
  --paddings '2cm 1cm 2cm 1.5cm' \
  --styles src/assets/starlight2pdf/body.css

test -s dist/downloads/delugecommunity.com.pdf
