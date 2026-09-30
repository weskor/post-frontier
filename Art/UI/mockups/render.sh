#!/usr/bin/env bash
# Render every mockup page (or the ones named) to a 1920x1080 PNG next to its HTML.
#   Art/UI/mockups/render.sh            all pages
#   Art/UI/mockups/render.sh 02-barracks-frontline
# --allow-file-access-from-files is required: the glyph masks and 9-slice frames load from ../ over file://.
set -euo pipefail
cd "$(dirname "$0")"
pages=("$@")
[ ${#pages[@]} -eq 0 ] && pages=($(ls [0-9]*.html | sed 's/\.html$//'))
for p in "${pages[@]}"; do
  chromium --headless=new --no-sandbox --disable-gpu --hide-scrollbars --allow-file-access-from-files \
    --force-device-scale-factor=1 --window-size=1920,1080 --virtual-time-budget=4000 \
    --screenshot="$PWD/$p.png" "file://$PWD/$p.html" >/dev/null 2>&1
  echo "rendered $p.png"
done
