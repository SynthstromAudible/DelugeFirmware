#!/usr/bin/env bash
set -euo pipefail

# Ubuntu's AppArmor policy blocks user namespaces for downloaded Chromium builds.
# Allow only our installed browser, keeping Chromium's own sandbox enabled.
# https://chromium.googlesource.com/chromium/src/+/main/docs/security/apparmor-userns-restrictions.md
browser_executable=$(node --input-type=module -e 'import { chromium } from "playwright"; console.log(chromium.executablePath())')
test -x "$browser_executable"

sudo tee /etc/apparmor.d/deluge-pdf-chromium > /dev/null <<EOF_PROFILE
abi <abi/4.0>,
include <tunables/global>

profile deluge-pdf-chromium "$browser_executable" flags=(unconfined) {
  userns,
}
EOF_PROFILE
sudo apparmor_parser -r /etc/apparmor.d/deluge-pdf-chromium
