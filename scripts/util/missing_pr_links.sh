#!/bin/bash

# Quick & dirty script to identify missing PR links from community features doc

set -euo pipefail

grep -o "\[#[0123456789]*\][^:]" docs/community_features.md |
    cut -d "#" -f 2 | cut -d "]" -f 1 |
    while read n; do
        if ! grep -q "\[#$n\]: https" docs/community_features.md; then
            echo "[#$n]: https://github.com/SynthstromAudible/DelugeFirmware/pull/$n"
        fi
    done
