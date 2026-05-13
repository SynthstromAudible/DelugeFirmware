#!/usr/bin/env bash
# Build a Deluge-flavoured OpenOCD (renesas_spibsc driver included) and install it OVER the
# xPack openocd that DBT already ships in toolchain/v<N>/<sys>-<arch>/openocd. After this script
# runs there is one openocd binary on the system, it has our flash driver, and it lives on
# $PATH (dbtenv.sh already injects toolchain/.../openocd/bin into PATH for every dbt task).
#
# Re-running ./dbt may re-fetch the xPack toolchain archive and overwrite our binary; in that
# case just re-run this script. Override OPENOCD_PREFIX to install elsewhere.
set -euo pipefail

cd "$(dirname "$0")/../../../.."
ROOT="$(pwd)"

# --- detect the DBT toolchain arch dir (mirrors dbtenv.sh logic). ---
SYS_TYPE="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH_TYPE="$(uname -m | tr '[:upper:]' '[:lower:]')"
[ "$ARCH_TYPE" = "aarch64" ] && ARCH_TYPE="arm64"

# Windows users: build-from-source on win32 is a separate effort (the DBT-shipped openocd is a
# prebuilt openocd-msvc). Bail with a clear message rather than fail mysteriously inside ./configure.
case "$SYS_TYPE" in
    mingw*|msys*|cygwin*|windows*)
        echo "FATAL: build-openocd.sh doesn't support Windows builds yet." >&2
        echo "On Windows the DBT toolchain ships a prebuilt xPack openocd-msvc — to add the" >&2
        echo "renesas_spibsc driver you'd need to rebuild that binary with our patch applied," >&2
        echo "which is a Visual-Studio + msys2 + autotools side-project we haven't tackled." >&2
        echo "Run this script on Linux or macOS and copy the resulting openocd.exe over." >&2
        exit 1
        ;;
esac

# Cross-platform parallel-job count.
if command -v nproc >/dev/null 2>&1; then
    NJOBS="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
    NJOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
else
    NJOBS=4
fi

TOOLCHAIN_VERSION="$(cat toolchain/REQUIRED_VERSION 2>/dev/null || true)"
DEFAULT_PREFIX="$ROOT/toolchain/v${TOOLCHAIN_VERSION}/${SYS_TYPE}-${ARCH_TYPE}/openocd"

OPENOCD_REPO="${OPENOCD_REPO:-https://github.com/openocd-org/openocd.git}"
OPENOCD_TAG="${OPENOCD_TAG:-v0.12.0}"
BUILD_DIR="${OPENOCD_BUILD_DIR:-build/openocd-spibsc}"
OPENOCD_PREFIX="${OPENOCD_PREFIX:-$DEFAULT_PREFIX}"
PATCH="$ROOT/scripts/debug/openocd/spibsc/openocd-0.12.0-spibsc.patch"

if [ ! -d "$OPENOCD_PREFIX" ]; then
    echo "FATAL: install prefix '$OPENOCD_PREFIX' does not exist." >&2
    echo "Run './dbt' once first to fetch the DBT toolchain, or set OPENOCD_PREFIX manually." >&2
    exit 1
fi

# --- fetch source ---
if [ ! -d "$BUILD_DIR/.git" ]; then
    echo "==> Cloning $OPENOCD_REPO @ $OPENOCD_TAG into $BUILD_DIR/"
    mkdir -p "$(dirname "$BUILD_DIR")"
    git clone --recurse-submodules --depth 1 --branch "$OPENOCD_TAG" \
        "$OPENOCD_REPO" "$BUILD_DIR"
fi

cd "$BUILD_DIR"

# --- apply our patch (idempotent: skip if driver source is already in place) ---
if [ ! -f src/flash/nor/renesas_spibsc.c ]; then
    echo "==> Applying SPIBSC driver patch"
    patch -p1 < "$PATCH"
fi

# --- configure & build, then `make install` over the xPack tree ---
if [ ! -f src/openocd ] || [ ! -f config.status ]; then
    echo "==> ./bootstrap"
    ./bootstrap
    echo "==> ./configure --prefix=$OPENOCD_PREFIX"
    # Default: enable CMSIS-DAP (delugeprobe), match xPack's enabled adapters reasonably.
    # Override with OPENOCD_CONFIGURE_FLAGS.
    ./configure --prefix="$OPENOCD_PREFIX" \
        ${OPENOCD_CONFIGURE_FLAGS:---enable-cmsis-dap --disable-werror}
fi

echo "==> make ($NJOBS jobs)"
make -j"$NJOBS"

echo "==> make install (replacing xPack openocd at $OPENOCD_PREFIX)"
make install

echo
echo "Installed: $OPENOCD_PREFIX/bin/openocd"
"$OPENOCD_PREFIX/bin/openocd" --version 2>&1 | head -1
echo
echo "Now 'make flash' will use this binary (via dbtenv.sh PATH injection)."
