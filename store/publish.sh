#!/usr/bin/env bash
# Uploads the current build to the Pebble appstore dashboard (NOT public:
# without --is-published the release stays in the dashboard until it is
# published there). Requires a prior `pebble login`.
# Usage: store/publish.sh            (from anywhere)
set -euo pipefail
export PATH=$HOME/.local/bin:$PATH
cd "$(dirname "$0")/.."
pebble login --status
pebble publish \
  --non-interactive \
  --name "CasioCGM" \
  --description "$(cat store/description.txt)" \
  --source "https://github.com/sgitaize/casiocgm" \
  --release-notes "$(cat store/release-notes.txt)" \
  --screenshots store/emery_1_in_range.png store/emery_2_high.png store/emery_3_mmol.png
