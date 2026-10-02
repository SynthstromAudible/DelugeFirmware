# VENDOR.md — block-device-driver

## Provenance

- **Source repo:** https://github.com/MabezDev/embedded-fatfs
- **Vendored rev:** `518528cc111fcf65c48abbdeb80735a38eada112` (short: `518528c`), same rev already
  vendored for `crates/embedded-fatfs` (SP0).
- **Vendored subtree:** the whole `block-device-driver/` crate from the upstream workspace —
  `Cargo.toml`, `src/lib.rs`, `LICENSE-APACHE`, `LICENSE-MIT`, `README.md`. This is the trait crate
  (`BlockDevice`) that `block-device-adapters` and, eventually, our SD-card block-device impl build on.
- **License:** dual MIT/Apache-2.0 upstream (`Cargo.toml` says `license = "MIT"`; both `LICENSE-MIT`
  and `LICENSE-APACHE` files are present upstream and preserved verbatim here). Compatible with this
  repo's GPL-3.0.
- **Dependencies:** only `aligned = "0.4.2"`, kept as a normal crates.io dependency (not vendored).
- **Workspace isolation:** `crates/block-device-driver/Cargo.toml` carries an empty `[workspace]`
  table so this directory is its own detached workspace root — it does not attach to (and cannot
  perturb) `crates/Cargo.toml`'s workspace/lockfile, which feeds the firmware build. It is
  intentionally NOT listed in `crates/Cargo.toml`'s `members`.
- **Not vendored:** upstream `tests/`, `examples/` (none present in this crate anyway) — the crate is
  a single trait definition in `src/lib.rs`.

## Why own the code instead of a git dependency

Same rationale as `crates/embedded-fatfs` (see that crate's `VENDOR.md`): `block-device-driver` is
unpublished on crates.io (only available via the upstream git checkout), so we vendor it as an owned
path dependency to carry our own hardening commits without depending on an upstream repo we don't
control. This crate itself required no fixes (the two upstream bugs relevant to this project — PR #68
and #62 — are both in `block-device-adapters`, not here); it is vendored solely because
`block-device-adapters` path-depends on it.

## Applied upstream PR fixes

None — no fixes target `block-device-driver`.
