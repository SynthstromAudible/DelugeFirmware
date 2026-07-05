# Design: Wire the bundled rustup into DBT (`dbtenv.sh`)

**Date:** 2026-07-05
**Status:** Approved (design); implementation plan to follow.

## Problem

DelugeFirmware builds Rust code (host `crates/` tests + the bare-metal
`armv7a-none-eabihf` firmware link, driven by a CMake `add_custom_command` that
shells out to `cargo build`). The dbt-toolchain now ships `rustup-init` (the Rust
bootstrapper) inside each toolpack at
`<toolchain>/rustup-init/rustup-init[.exe]` (added in toolchain **v25**), but
DBT's environment setup (`scripts/toolchain/dbtenv.sh`) does not yet put a
`cargo`/`rustc` on `PATH`. The device build therefore has no Rust unless the
developer installs one globally, defeating the portable, self-contained toolchain
model DBT uses for cmake/ninja/gcc/python/clang-format.

This is the DelugeFirmware-side follow-up defined by the dbt-toolchain Rust spec
(`dbt-toolchain/docs/superpowers/specs/2026-07-04-rust-rustup-toolchain-design.md`,
§3 "Integration contract").

## Contract to implement (from the dbt-toolchain spec §3)

```
Environment (point rustup/cargo state inside the toolchain tree):
  RUSTUP_HOME = <toolchain>/rust/rustup
  CARGO_HOME  = <toolchain>/rust/cargo

One-time bootstrap (first build):
  rustup-init -y --no-modify-path --profile minimal --default-toolchain none

Every build:
  prepend  $CARGO_HOME/bin  to PATH
```

`--default-toolchain none` makes each `rust-toolchain.toml` (`crates/`,
`src/bsp/rust/`) the single source of truth; the first `cargo`/`rustc` call
installs the pinned nightly + components + targets. The heavy toolchain is never
bundled — it lands in `<toolchain>/rust` on first build.

## Where it fits

`scripts/toolchain/dbtenv.sh` (sourced by `./dbt`) computes
`TOOLCHAIN_ARCH_DIR="${DBT_TOOLCHAIN_PATH}/toolchain/v${VERSION}/${sys}-${arch}"`
— this is the `<toolchain>` root — and `dbtenv_main()` prepends each tool's `bin`
to `PATH`, with `dbtenv_restore_env()` stripping them back out. The rustup wiring
mirrors this pattern exactly.

No firmware CI changes are needed:

- `.github/workflows/rust.yml` (host `crates/` fmt/clippy/nextest) uses the GitHub
  runner's **ambient** rustup, independent of the dbt toolchain. Untouched.
- `.github/workflows/build.yml` (device build) runs *inside*
  `ghcr.io/synthstromaudible/dbt-toolchain:v$(cat toolchain/REQUIRED_VERSION)`,
  which sources `dbtenv.sh` — so the wiring flows through automatically.

## Decisions (from brainstorming)

| Decision | Choice |
|---|---|
| Activation | Bump `toolchain/REQUIRED_VERSION` 23 → 25 (the version that ships `rustup-init`), activating Rust for all developers. |
| Safety guard | The wiring self-guards on `rustup-init` existing, so it is a no-op on pre-v25 toolchains rather than an error. |
| Bootstrap trigger | Idempotent: bootstrap when `$CARGO_HOME/bin/rustup` is absent, not tied to fresh-unpack. |
| Bootstrap failure | Best-effort: warn and continue (a flaky network must not brick all of `./dbt`); a later `cargo` call fails loudly. |

## Changes

### `scripts/toolchain/dbtenv.sh`

**New function `dbtenv_setup_rust()`**, called from `dbtenv_main()` immediately
after the SSL/cert env is exported (so bootstrap has CA certs available):

1. `RUSTUP_INIT="$TOOLCHAIN_ARCH_DIR/rustup-init/rustup-init"`. If it is not
   executable, `return 0` (pre-v25 toolchain — no-op).
2. Save prior values: `SAVED_RUSTUP_HOME`, `SAVED_CARGO_HOME`. Then
   `export RUSTUP_HOME="$TOOLCHAIN_ARCH_DIR/rust/rustup"` and
   `export CARGO_HOME="$TOOLCHAIN_ARCH_DIR/rust/cargo"`.
3. One-time bootstrap: if `$CARGO_HOME/bin/rustup` is missing, run
   `"$RUSTUP_INIT" -y --no-modify-path --profile minimal --default-toolchain none`.
   On failure, print a warning and continue (do not abort sourcing).
4. `PATH="$CARGO_HOME/bin:$PATH"; export PATH`.
5. `return 0`.

**`dbtenv_restore_env()`** additions:

- Strip `$TOOLCHAIN_ARCH_DIR/rust/cargo/bin` from `PATH` (same `sed` pattern the
  other tool paths use).
- Restore `RUSTUP_HOME`/`CARGO_HOME` from `SAVED_*` when set, else unset them;
  then unset the `SAVED_*` — using `${SAVED_RUSTUP_HOME:-""}`-style defaults so it
  is safe under `set -u` even when rust setup never ran.

**Testability seam:** guard the file's bottom `dbtenv_main` invocation:

```sh
if [ -z "${DBTENV_LIB_ONLY:-}" ]; then
    dbtenv_main "${1:-""}";
fi
```

so the functions can be sourced and tested without running the full
download/env flow.

### `toolchain/REQUIRED_VERSION`

`23` → `25`.

## Error handling

- Missing `rustup-init` (old toolchain): silent no-op via the guard.
- `rustup-init` bootstrap failure (e.g. no network): warning printed, sourcing
  continues; Rust build steps fail later at `cargo` with a clear error. Non-Rust
  DBT tasks remain usable.
- `set -u` safety: all `SAVED_*` reads in restore use `:-` defaults.

## Testing

- **New `scripts/toolchain/test_dbtenv_rust.sh`** (pure bash, no network): sources
  `dbtenv.sh` with `DBTENV_LIB_ONLY=1`; sets `TOOLCHAIN_ARCH_DIR` to a temp dir
  containing a **stub** `rustup-init/rustup-init` that creates a fake
  `$CARGO_HOME/bin/rustup`. Asserts:
  1. `dbtenv_setup_rust` sets `RUSTUP_HOME`/`CARGO_HOME` to the in-tree paths.
  2. Bootstrap runs once (stub invoked, shim created) and `$CARGO_HOME/bin` is on
     `PATH`.
  3. A second call is idempotent (shim present → stub not re-invoked).
  4. With no `rustup-init`, the function returns 0 and leaks no env/PATH changes.
  5. `dbtenv_restore_env` removes `…/rust/cargo/bin` from `PATH` and unsets
     `RUSTUP_HOME`/`CARGO_HOME`.
- **`shellcheck scripts/toolchain/dbtenv.sh`** stays clean (respect the existing
  `# shellcheck disable` directives at the top of the file).
- **End-to-end** (`./dbt build` producing an armv7a Rust link) is deferred to CI /
  a machine with a v25 toolchain + network; it is not reproducible in this
  workspace (no Docker, no v25 tarball locally).

## Risks / prerequisites

1. **v25 must be published before this reaches a build branch.** Bumping
   `REQUIRED_VERSION=25` makes `dbtenv.sh` download
   `github.com/SynthstromAudible/dbt-toolchain/releases/download/v25/…` and
   `build.yml` pull `ghcr.io/synthstromaudible/dbt-toolchain:v25`. The v25
   toolchain (with `rustup-init`) is not yet released, so until it is, `./dbt` and
   `build.yml` will 404. Land this in lockstep with, or after, the v25 release.
2. **Branch reconciliation.** Development is on `next`, but the dbt-toolchain
   CI-image and firmware `build.yml`/`rust.yml` key off `main`. This change must
   reach `main` to be exercised there.
3. **Windows host Rust** (`x86_64-pc-windows-msvc` expects MSVC `link.exe`) — an
   open risk noted in the dbt-toolchain spec §4. The armv7a firmware cross-link
   uses lld/gcc, so it should not block the device build; host-side Rust tests on
   Windows may. Out of scope here.

## Out of scope

- Pinning `nightly-YYYY-MM-DD` in the `rust-toolchain.toml` files (reproducibility
  is the firmware repo's separate call).
- Wiring the bare-metal BSP clippy/build job in `rust.yml` (its own TODO there).
- Any dbt-toolchain-side change (already handled in that repo).
