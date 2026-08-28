//! The BSP's audio-loader host tests, compiled where CI can reach them.
//!
//! # Do not remove the `#[rustfmt::skip]` below
//!
//! **rustfmt follows `#[path]`, so without it a `cargo fmt` run scoped to
//! `crates/` REWRITES `src/bsp/rust/src/audio_loader.rs` and
//! `src/bsp/rust/tests/audio_loader_host.rs` in place** — production firmware
//! source, outside the invoked manifest's tree, with nothing in the output
//! saying it happened. An editor's format-on-save pointed at this workspace
//! would produce silent, unreviewed diffs to BSP code. The attribute stops
//! rustfmt descending into the module and confines it to this tree.
//!
//! Nothing is lost by skipping: both files are already formatted by the
//! `rustfmt (bsp)` CI step, which — unlike everything else about this manifest —
//! works fine on a checkout with no sibling `deluge-sdk`, because `cargo fmt`
//! needs the target list rather than a resolved dependency graph. Verified by
//! injecting a formatting violation into each file on such a checkout: that step
//! exits 1 for both.
//!
//! # What this file is
//!
//! Nothing is reimplemented here. `src/bsp/rust/tests/audio_loader_host.rs` is
//! included verbatim through `#[path]`, and it in turn `#[path]`-includes
//! `src/bsp/rust/src/audio_loader.rs`; both relative paths resolve against the
//! included file's real location, so the nesting needs no help from this side.
//! Every `#[test]` in that file is registered in this binary's harness.
//!
//! The included file is a crate root in its own package and a module here. That
//! is the only difference, and it costs nothing: its `#![cfg(...)]` inner
//! attributes gate the module instead of the crate, which selects the same
//! source either way.
//!
//! # Bumping a version in `src/bsp/rust/Cargo.toml`? Mirror it here
//!
//! Three dependencies are duplicated literals across the two manifests, with
//! nothing holding them equal: **`heapless`, `embassy-sync`, and
//! `embassy-futures`**. (`deluge_audio_format` is exempt — it is a path
//! dependency to the same directory from both, so it cannot drift.)
//!
//! Drift that changes an API fails loudly and immediately, which is fine. The
//! case to watch is the quiet one: a BSP bump to a version with the same API and
//! different runtime semantics leaves this package compiling, passing, and
//! validating behaviour that no longer ships. `heapless` is the likeliest —
//! `crates/` already resolves 0.9 through embassy-sync while the BSP and this
//! package pin 0.8, so the two are deliberately, not accidentally, apart.
#[rustfmt::skip]
#[path = "../../../src/bsp/rust/tests/audio_loader_host.rs"]
mod audio_loader_host;
