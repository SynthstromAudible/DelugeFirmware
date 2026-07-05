# DBT rustup Wiring Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `./dbt` bootstrap and expose the toolchain-bundled rustup so `cargo`/`rustc` are on `PATH` for the DelugeFirmware build, with Rust state contained inside the portable toolchain tree.

**Architecture:** Add two self-contained functions to `scripts/toolchain/dbtenv.sh` — `dbtenv_setup_rust` (guarded bootstrap + env + `PATH`) called from `dbtenv_main`, and `dbtenv_restore_rust` (unwind) called from `dbtenv_restore_env`. A `DBTENV_LIB_ONLY` seam lets the functions be sourced for a pure-bash unit test that uses a stub `rustup-init`. Separately, bump `toolchain/REQUIRED_VERSION` 23 → 25 to activate it.

**Tech Stack:** POSIX/bash shell, rustup (`rustup-init`), the DBT toolchain env script.

## Global Constraints

- **Contract (verbatim from spec §"Contract to implement"):**
  - `RUSTUP_HOME = <toolchain>/rust/rustup`, `CARGO_HOME = <toolchain>/rust/cargo`, where `<toolchain>` is `$TOOLCHAIN_ARCH_DIR`.
  - Bootstrap: `rustup-init -y --no-modify-path --profile minimal --default-toolchain none`.
  - Every build: prepend `$CARGO_HOME/bin` to `PATH`.
- **Guard:** do nothing if `$TOOLCHAIN_ARCH_DIR/rustup-init/rustup-init` is not executable (pre-v25 toolchains must keep working).
- **Bootstrap trigger is idempotent:** bootstrap only when `$CARGO_HOME/bin/rustup` is absent (not tied to fresh-unpack).
- **Bootstrap failure is best-effort:** print a warning and continue; never abort sourcing.
- **`set -u` safety:** every `SAVED_*` read uses `${VAR:-...}` / `${VAR+x}` defaults; `dbtenv.sh` is sourced by `./dbt` under `set -eu`.
- **Follow existing `dbtenv.sh` style:** no `local`, `;`-terminated lines, `/usr/bin/sed` for PATH strips — match the surrounding code.
- **Do NOT touch** `crates/rust-toolchain.toml`, `src/bsp/rust/rust-toolchain.toml`, any `.github/workflows/*`, or CMake. The wiring flows into the device build automatically (build.yml runs inside the dbt-toolchain image, which sources this script).
- **Prerequisite for Task 2 to be safe:** toolchain **v25** (which ships `rustup-init`) must be published at `github.com/SynthstromAudible/dbt-toolchain` releases and as `ghcr.io/synthstromaudible/dbt-toolchain:v25`, or `./dbt` and `build.yml` will 404. Task 1 is safe to land regardless (it is dormant on pre-v25 toolchains).

---

### Task 1: Add the rustup wiring to `dbtenv.sh` (test-driven)

**Files:**
- Modify: `scripts/toolchain/dbtenv.sh` (add `dbtenv_setup_rust` + `dbtenv_restore_rust`; call them from `dbtenv_main` / `dbtenv_restore_env`; add the `DBTENV_LIB_ONLY` guard on the bottom `dbtenv_main` call)
- Create: `scripts/toolchain/test_dbtenv_rust.sh` (pure-bash unit test)

**Interfaces:**
- Consumes: the global `TOOLCHAIN_ARCH_DIR` (set by `dbtenv_get_kernel_type`), and the file-level `dbtenv_main` invocation.
- Produces:
  - `dbtenv_setup_rust` — with `TOOLCHAIN_ARCH_DIR` set: no-op unless `$TOOLCHAIN_ARCH_DIR/rustup-init/rustup-init` is executable; otherwise exports `RUSTUP_HOME`/`CARGO_HOME` under `$TOOLCHAIN_ARCH_DIR/rust/{rustup,cargo}`, bootstraps rustup once when `$CARGO_HOME/bin/rustup` is missing, and prepends `$CARGO_HOME/bin` to `PATH`. Saves prior values in `SAVED_RUSTUP_HOME`/`SAVED_CARGO_HOME`.
  - `dbtenv_restore_rust` — unwinds the above only when `SAVED_RUSTUP_HOME` is set: strips `$TOOLCHAIN_ARCH_DIR/rust/cargo/bin` from `PATH`, restores or unsets `RUSTUP_HOME`/`CARGO_HOME`, clears the `SAVED_*`.
  - Sourcing with `DBTENV_LIB_ONLY=1` defines the functions without running `dbtenv_main`.

- [ ] **Step 1: Add the `DBTENV_LIB_ONLY` seam so the script can be sourced for testing**

In `scripts/toolchain/dbtenv.sh`, replace the final line:

```sh
dbtenv_main "${1:-""}";
```

with:

```sh
# Allow tests to source this file for its functions without running the full
# environment setup (download, PATH mutation, etc).
if [ -z "${DBTENV_LIB_ONLY:-}" ]; then
    dbtenv_main "${1:-""}";
fi
```

- [ ] **Step 2: Write the failing test — `scripts/toolchain/test_dbtenv_rust.sh`**

```bash
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
```

Then make it executable:

```bash
chmod +x scripts/toolchain/test_dbtenv_rust.sh
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `./scripts/toolchain/test_dbtenv_rust.sh`
Expected: FAIL — the run aborts or reports failures because `dbtenv_setup_rust` / `dbtenv_restore_rust` are undefined (`command not found`). (The `DBTENV_LIB_ONLY` guard from Step 1 lets the source succeed without triggering a toolchain download.)

- [ ] **Step 4: Implement the two functions and wire them in**

**(4a)** Add both functions. In `scripts/toolchain/dbtenv.sh`, immediately after the `dbtenv_setup_python()` function's closing brace, insert:

```sh
dbtenv_setup_rust()
{
    RUSTUP_INIT="$TOOLCHAIN_ARCH_DIR/rustup-init/rustup-init";

    # Toolchains before v25 don't ship rustup-init; leave the environment
    # untouched so older toolchains keep working.
    if [ ! -x "$RUSTUP_INIT" ]; then
        return 0;
    fi

    # Keep rustup/cargo state inside the portable toolchain tree, not the
    # developer's ~/.rustup / ~/.cargo. Save the caller's prior values once, so a
    # re-source doesn't clobber the originals (restore relies on this).
    if [ -z "${SAVED_RUSTUP_HOME+x}" ]; then
        export SAVED_RUSTUP_HOME="${RUSTUP_HOME:-""}";
        export SAVED_CARGO_HOME="${CARGO_HOME:-""}";
    fi
    export RUSTUP_HOME="$TOOLCHAIN_ARCH_DIR/rust/rustup";
    export CARGO_HOME="$TOOLCHAIN_ARCH_DIR/rust/cargo";

    # One-time bootstrap: lay down the rustup/cargo shims. --default-toolchain
    # none makes each rust-toolchain.toml the single source of truth; the first
    # cargo call installs the pinned nightly + components + targets. Best-effort:
    # a network failure here must not brick the rest of the DBT environment.
    if [ ! -x "$CARGO_HOME/bin/rustup" ]; then
        echo "DBT: bootstrapping rustup (one-time)..";
        if ! "$RUSTUP_INIT" -y --no-modify-path --profile minimal --default-toolchain none; then
            echo "DBT: warning: rustup bootstrap failed; Rust builds will not work until it succeeds.";
        fi
    fi

    PATH="$CARGO_HOME/bin:$PATH";
    export PATH;
    return 0;
}

dbtenv_restore_rust()
{
    # Only unwind if this session set rust up (SAVED_RUSTUP_HOME will be set).
    if [ -z "${SAVED_RUSTUP_HOME+x}" ]; then
        return 0;
    fi

    TOOLCHAIN_ARCH_DIR_SED="$(echo "$TOOLCHAIN_ARCH_DIR" | sed 's/\//\\\//g')";
    PATH="$(echo "$PATH" | /usr/bin/sed "s/$TOOLCHAIN_ARCH_DIR_SED\/rust\/cargo\/bin://g")";

    if [ -n "$SAVED_RUSTUP_HOME" ]; then
        export RUSTUP_HOME="$SAVED_RUSTUP_HOME";
        export CARGO_HOME="$SAVED_CARGO_HOME";
    else
        unset RUSTUP_HOME;
        unset CARGO_HOME;
    fi
    unset SAVED_RUSTUP_HOME;
    unset SAVED_CARGO_HOME;
}
```

**(4b)** Call `dbtenv_setup_rust` from `dbtenv_main`, after the SSL/cert env is exported. Replace:

```sh
    export SSL_CERT_FILE=$(python3 -c 'import site; print(site.getsitepackages()[-1])')/certifi/cacert.pem
    export REQUESTS_CA_BUNDLE="$SSL_CERT_FILE";

    if [ -n "${DBT_DID_UNPACKING}" ]; then
```

with:

```sh
    export SSL_CERT_FILE=$(python3 -c 'import site; print(site.getsitepackages()[-1])')/certifi/cacert.pem
    export REQUESTS_CA_BUNDLE="$SSL_CERT_FILE";

    dbtenv_setup_rust;

    if [ -n "${DBT_DID_UNPACKING}" ]; then
```

**(4c)** Call `dbtenv_restore_rust` from `dbtenv_restore_env`. Replace:

```sh
    PATH="$(echo "$PATH" | /usr/bin/sed "s/$TOOLCHAIN_ARCH_DIR_SED\/ninja-build\/bin://g")";
    # PATH="$(echo "$PATH" | /usr/bin/sed "s/$TOOLCHAIN_ARCH_DIR_SED\/openssl\/bin://g")";
```

with:

```sh
    PATH="$(echo "$PATH" | /usr/bin/sed "s/$TOOLCHAIN_ARCH_DIR_SED\/ninja-build\/bin://g")";
    # PATH="$(echo "$PATH" | /usr/bin/sed "s/$TOOLCHAIN_ARCH_DIR_SED\/openssl\/bin://g")";
    dbtenv_restore_rust;
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `./scripts/toolchain/test_dbtenv_rust.sh`
Expected: every `ok:` line prints and the script ends with `ALL PASS` (exit 0).

- [ ] **Step 6: Static-check the script**

Run: `shellcheck scripts/toolchain/dbtenv.sh scripts/toolchain/test_dbtenv_rust.sh`
Expected: no new warnings beyond those already suppressed by the `# shellcheck disable=...` header in `dbtenv.sh`. If `shellcheck` is not installed, skip this step (note it in the commit).

- [ ] **Step 7: Commit**

```bash
git add scripts/toolchain/dbtenv.sh scripts/toolchain/test_dbtenv_rust.sh
git commit -m "Wire bundled rustup into DBT env setup

dbtenv.sh now bootstraps the toolchain-bundled rustup-init (RUSTUP_HOME/
CARGO_HOME inside the toolchain tree, --default-toolchain none) and puts
\$CARGO_HOME/bin on PATH, so ./dbt provides cargo/rustc for the firmware build.
Guarded on rustup-init existing (no-op on pre-v25 toolchains) and idempotent.
Adds a pure-bash unit test via a DBTENV_LIB_ONLY source seam."
```

---

### Task 2: Activate Rust by bumping the required toolchain version

Kept as a separate commit so it can be timed with the v25 toolchain release (see Global Constraints prerequisite): the wiring from Task 1 is dormant until a v25 toolchain is in use.

**Files:**
- Modify: `toolchain/REQUIRED_VERSION`

**Interfaces:**
- Consumes: the Task 1 wiring (active once a v25 toolchain, which ships `rustup-init`, is selected).
- Produces: `./dbt` selects toolchain v25 (`.../toolchain/v25/<sys>-<arch>`), and `build.yml` pulls `ghcr.io/synthstromaudible/dbt-toolchain:v25`.

- [ ] **Step 1: Confirm the current value**

Run: `cat toolchain/REQUIRED_VERSION`
Expected: `23`.

- [ ] **Step 2: Bump to 25**

Replace the entire contents of `toolchain/REQUIRED_VERSION` with (preserve the trailing newline):

```
25
```

- [ ] **Step 3: Verify**

Run: `cat toolchain/REQUIRED_VERSION`
Expected: `25`.

- [ ] **Step 4: Commit**

```bash
git add toolchain/REQUIRED_VERSION
git commit -m "Require toolchain v25 (ships rustup-init) to activate Rust

Selects the first toolchain that bundles rustup-init, activating the dbtenv.sh
rustup wiring for all developers. Depends on v25 being published at
SynthstromAudible/dbt-toolchain (releases + ghcr image)."
```

---

## Notes for the implementer

- **Why the guard and the version bump are separate commits:** Task 1 is safe to merge any time (dormant on v22/v23/v24). Task 2 is the activation switch and must not land before the v25 toolchain is published, or `./dbt` and `build.yml` will fail to fetch it. If v25 is not yet released when you execute, do Task 1 and hold Task 2.
- **No real end-to-end here:** an actual `./dbt build` producing an armv7a Rust link needs a v25 toolchain plus network for the first `cargo` fetch, and is not reproducible in this workspace (no Docker, no v25 tarball). The authoritative end-to-end is the dbt-toolchain CI-image smoke build (which now tracks firmware `main` and ships v25 with `rustup-init`). The Task 1 unit test covers the wiring logic (env, idempotent bootstrap, PATH, guard, restore) without network.
- **Branch target:** this work is on `rust/dbt-rustup-wiring` off `next`. It must ultimately reach `main` (where `rust.yml`/`build.yml` and the dbt-toolchain CI key off) to be exercised end-to-end. Reconciling `next` vs `main` is a repo-workflow decision for the merge step, not part of these edits.
- **Style:** `dbtenv.sh` deliberately avoids `local` and terminates statements with `;`. Match it; the file's top `# shellcheck disable=SC2034,SC2016,SC2086` covers the global-variable and quoting patterns used here.
