//! The BSP's audio-loader host tests, compiled where CI can reach them.
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
//! One consequence to know about: rustfmt follows `#[path]` too, so CI's
//! `rustfmt (crates)` step now also checks both BSP files, alongside the
//! `rustfmt (bsp)` step that already did. Harmless as things stand — neither
//! tree carries a `rustfmt.toml`, so both steps apply the identical default
//! config and cannot disagree. Giving `crates/` or `src/bsp/rust/` its own
//! config would end that, and would need this coupling resolved first.
#[path = "../../../src/bsp/rust/tests/audio_loader_host.rs"]
mod audio_loader_host;
