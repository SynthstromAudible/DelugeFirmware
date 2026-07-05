#!/usr/bin/env bash
# Unit test for the rustup wiring in dbtenv.sh (dbtenv_setup_rust /
# dbtenv_restore_rust). Pure bash, no network: a stub rustup-init stands in for
# the real bootstrapper.
set -u

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd -P)"
cd "$REPO_ROOT"

FAILS=0
fail() { echo "FAIL: $1"; FAILS=$((FAILS + 1)); }
pass() { echo "ok: $1"; }

# Source the library for its functions without running dbtenv_main.
DBTENV_LIB_ONLY=1 . "$REPO_ROOT/scripts/toolchain/dbtenv.sh"

# --- fixture: a fake toolchain arch dir with a stub rustup-init ---------------
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
TOOLCHAIN_ARCH_DIR="$WORK/toolchain/v25/linux-x86_64"
mkdir -p "$TOOLCHAIN_ARCH_DIR/rustup-init"
cat > "$TOOLCHAIN_ARCH_DIR/rustup-init/rustup-init" <<'STUB'
#!/usr/bin/env bash
# Stub rustup-init: record the invocation and lay down a fake cargo/rustup shim.
mkdir -p "$CARGO_HOME/bin"
echo "invoked" >> "$CARGO_HOME/.bootstrap-count"
printf '#!/bin/sh\n' > "$CARGO_HOME/bin/rustup"
chmod +x "$CARGO_HOME/bin/rustup"
STUB
chmod +x "$TOOLCHAIN_ARCH_DIR/rustup-init/rustup-init"

# --- test 1: setup sets env, bootstraps once, adds cargo bin to PATH ----------
dbtenv_setup_rust
[ "${RUSTUP_HOME:-}" = "$TOOLCHAIN_ARCH_DIR/rust/rustup" ] && pass "RUSTUP_HOME set" || fail "RUSTUP_HOME=${RUSTUP_HOME:-unset}"
[ "${CARGO_HOME:-}" = "$TOOLCHAIN_ARCH_DIR/rust/cargo" ] && pass "CARGO_HOME set" || fail "CARGO_HOME=${CARGO_HOME:-unset}"
case ":$PATH:" in *":$CARGO_HOME/bin:"*) pass "cargo bin on PATH";; *) fail "cargo bin not on PATH";; esac
[ -x "$CARGO_HOME/bin/rustup" ] && pass "shim created" || fail "shim missing"
count="$(wc -l < "$CARGO_HOME/.bootstrap-count" 2>/dev/null || echo 0)"
[ "$count" -eq 1 ] && pass "bootstrap ran once" || fail "bootstrap count=$count (want 1)"

# --- test 2: idempotent — second call does not re-bootstrap -------------------
dbtenv_setup_rust
count="$(wc -l < "$CARGO_HOME/.bootstrap-count" 2>/dev/null || echo 0)"
[ "$count" -eq 1 ] && pass "second call idempotent" || fail "re-bootstrapped, count=$count"

# --- test 3: restore removes cargo bin from PATH and unsets vars --------------
dbtenv_restore_rust
case ":$PATH:" in *":$TOOLCHAIN_ARCH_DIR/rust/cargo/bin:"*) fail "cargo bin still on PATH";; *) pass "cargo bin removed from PATH";; esac
[ -z "${RUSTUP_HOME:-}" ] && pass "RUSTUP_HOME unset" || fail "RUSTUP_HOME still set: ${RUSTUP_HOME:-}"
[ -z "${CARGO_HOME:-}" ] && pass "CARGO_HOME unset" || fail "CARGO_HOME still set: ${CARGO_HOME:-}"

# --- test 4: no rustup-init -> no-op, no env leak -----------------------------
unset RUSTUP_HOME CARGO_HOME SAVED_RUSTUP_HOME SAVED_CARGO_HOME 2>/dev/null || true
TOOLCHAIN_ARCH_DIR="$WORK/toolchain/v23/linux-x86_64"   # no rustup-init here
mkdir -p "$TOOLCHAIN_ARCH_DIR"
PATH_SNAP="$PATH"
dbtenv_setup_rust
[ -z "${RUSTUP_HOME:-}" ] && pass "guard: RUSTUP_HOME not set" || fail "guard leaked RUSTUP_HOME"
[ "$PATH" = "$PATH_SNAP" ] && pass "guard: PATH unchanged" || fail "guard changed PATH"

# -----------------------------------------------------------------------------
if [ "$FAILS" -eq 0 ]; then echo "ALL PASS"; exit 0; else echo "$FAILS FAILURE(S)"; exit 1; fi
