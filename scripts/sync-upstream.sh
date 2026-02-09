#!/bin/bash
# Sync the branch chain: upstream/main -> owlet-firmware-1.3 -> dev
#
# Branch hierarchy:
#   upstream/main (SynthstromAudible/DelugeFirmware)
#     └── owlet-firmware-1.3 (owlet-labs fork base)
#           └── dev (active development)
#
# Usage: ./scripts/sync-upstream.sh

set -euo pipefail

ORIG_BRANCH=$(git branch --show-current)

echo "Fetching upstream..."
git fetch upstream

echo ""
echo "Merging upstream/main into owlet-firmware-1.3..."
git checkout owlet-firmware-1.3
git merge upstream/main

echo ""
echo "Merging owlet-firmware-1.3 into dev..."
git checkout dev
git merge owlet-firmware-1.3

echo ""
echo "Sync complete. Returning to $ORIG_BRANCH..."
git checkout "$ORIG_BRANCH"

echo "Done."
