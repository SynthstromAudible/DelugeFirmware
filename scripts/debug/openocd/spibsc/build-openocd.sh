#!/usr/bin/env bash
# Fetch upstream OpenOCD 0.12.0, apply the Deluge SPIBSC driver patch, and build.
# Output: openocd-0.12.0/src/openocd (the binary task-flash.py expects).
set -euo pipefail

cd "$(dirname "$0")/../../../.."
ROOT="$(pwd)"

UPSTREAM_URL="https://downloads.sourceforge.net/project/openocd/openocd/0.12.0/openocd-0.12.0.tar.bz2"
UPSTREAM_SHA256="af254788be98861f2bd9103fe6e60a774ec96a8c374744eef9197f6043075afa"
TARBALL="openocd_0.12.0.orig.tar.bz2"
PATCH="$ROOT/scripts/debug/openocd/spibsc/openocd-0.12.0-spibsc.patch"

if [ ! -f "$TARBALL" ]; then
    echo "==> Fetching $UPSTREAM_URL"
    curl -fL -o "$TARBALL" "$UPSTREAM_URL"
fi

actual_sha=$(sha256sum "$TARBALL" | awk '{print $1}')
if [ "$actual_sha" != "$UPSTREAM_SHA256" ]; then
    echo "FATAL: $TARBALL sha256 mismatch" >&2
    echo "  expected: $UPSTREAM_SHA256" >&2
    echo "  actual:   $actual_sha" >&2
    exit 1
fi

if [ ! -d openocd-0.12.0 ]; then
    echo "==> Extracting $TARBALL"
    tar xjf "$TARBALL"
fi

if [ ! -f openocd-0.12.0/src/flash/nor/renesas_spibsc.c ]; then
    echo "==> Applying SPIBSC driver patch"
    # Patch was generated against pristine upstream; -p1 inside openocd-0.12.0/ strips the ocd-b/ prefix.
    (cd openocd-0.12.0 && patch -p1 < "$PATCH")
fi

if [ ! -x openocd-0.12.0/src/openocd ]; then
    cd openocd-0.12.0
    echo "==> ./bootstrap (regenerating autoconf for new flash driver)"
    ./bootstrap
    echo "==> ./configure"
    # Disable a few backends to minimise the dependency surface; the only thing this build needs is
    # CMSIS-DAP (delugeprobe). Add more flags via OPENOCD_CONFIGURE_FLAGS if needed.
    ./configure ${OPENOCD_CONFIGURE_FLAGS:---enable-cmsis-dap --disable-werror}
    echo "==> make ($(nproc) jobs)"
    make -j"$(nproc)"
fi

echo
echo "openocd binary: $ROOT/openocd-0.12.0/src/openocd"
echo "run 'make flash' from $ROOT to flash."
