#!/usr/bin/env bash
# Clone upstream OpenOCD at v0.12.0, apply the Deluge SPIBSC driver patch, and build.
# Output: openocd-0.12.0/src/openocd (the binary task-flash.py expects).
set -euo pipefail

cd "$(dirname "$0")/../../../.."
ROOT="$(pwd)"

OPENOCD_REPO="${OPENOCD_REPO:-https://github.com/openocd-org/openocd.git}"
OPENOCD_TAG="${OPENOCD_TAG:-v0.12.0}"
SRC_DIR="${OPENOCD_SRC_DIR:-openocd-0.12.0}"
PATCH="$ROOT/scripts/debug/openocd/spibsc/openocd-0.12.0-spibsc.patch"

if [ ! -d "$SRC_DIR/.git" ]; then
    echo "==> Cloning $OPENOCD_REPO @ $OPENOCD_TAG into $SRC_DIR/"
    git clone --recurse-submodules --depth 1 --branch "$OPENOCD_TAG" \
        "$OPENOCD_REPO" "$SRC_DIR"
fi

cd "$SRC_DIR"

# Idempotent patch apply: only apply if the driver isn't already there. Lets the user re-run this
# script to rebuild without re-fetching, while still applying cleanly on a fresh clone.
if [ ! -f src/flash/nor/renesas_spibsc.c ]; then
    echo "==> Applying SPIBSC driver patch"
    patch -p1 < "$PATCH"
fi

if [ ! -x src/openocd ]; then
    echo "==> ./bootstrap (regenerating autoconf for new flash driver)"
    ./bootstrap
    echo "==> ./configure"
    # Default: enable CMSIS-DAP (delugeprobe), disable -Werror (the renesas_rpchf source has a
    # warning under newer gcc). Override with OPENOCD_CONFIGURE_FLAGS.
    ./configure ${OPENOCD_CONFIGURE_FLAGS:---enable-cmsis-dap --disable-werror}
    echo "==> make ($(nproc) jobs)"
    make -j"$(nproc)"
fi

echo
echo "openocd binary: $ROOT/$SRC_DIR/src/openocd"
echo "run 'make flash' from $ROOT to flash."
