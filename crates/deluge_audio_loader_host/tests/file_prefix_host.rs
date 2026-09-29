//! The BSP's file-prefix host tests, compiled where CI can reach them.
//!
//! Same arrangement, and the same reasoning, as `tests/audio_loader_host.rs` beside it: nothing is
//! reimplemented, `src/bsp/rust/tests/file_prefix_host.rs` is included verbatim through `#[path]`,
//! and it in turn `#[path]`-includes `src/bsp/rust/src/file_prefix.rs` and the
//! `src/bsp/rust/src/audio_loader.rs` whose `LoadOps` the prefix driver reuses. Every relative path
//! resolves against the including file's real location, so the extra nesting needs no help from
//! here.
//!
//! Its own package cannot run it on a CI checkout: `src/bsp/rust`'s `[patch]` redirects
//! `deluge-bsp`/`rza1l-hal` to a sibling `deluge-sdk` tree that is not there. Without this shim the
//! suite would be unreachable by CI.
//!
//! # Do not remove the `#[rustfmt::skip]` below
//!
//! **rustfmt follows `#[path]`**, so without it a `cargo fmt` run scoped to `crates/` rewrites
//! production firmware source outside this manifest's tree, silently. See
//! `tests/audio_loader_host.rs`'s much longer note; it applies here unchanged.
#[rustfmt::skip]
#[path = "../../../src/bsp/rust/tests/file_prefix_host.rs"]
mod file_prefix_host;
