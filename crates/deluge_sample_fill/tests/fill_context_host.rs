//! Host round-trip test for the per-asset streaming fill-context table:
//! `deluge_streaming_set_fill_context` (the main/load-thread
//! write, called from C++ at sample-load) → `fill_context_for` (the read side the native fill task
//! wires in). Always-compiled tier (see the crate's `lib.rs` doc): this does NOT need `host_app` (it
//! doesn't touch `deluge_resource` — see the table's own doc in `lib.rs`).
//!
//! A normal integration test for this crate: exercises `deluge_sample_fill`'s public API as an
//! ordinary dependent, no `#[path]` recompilation needed (unlike `deluge-bsp-rust`'s bin-only
//! tests, which recompile `src/streaming_loader.rs` directly since that crate has no `[lib]` target).
#![cfg(not(target_os = "none"))]

use core::ptr;

use deluge_sample_fill::{FillContext, deluge_streaming_set_fill_context, fill_context_for};

fn sample_ctx() -> FillContext {
    FillContext {
        efatfs_handle: 7,
        audio_data_start_pos_bytes: 44,
        audio_data_length_bytes: 123_456,
        first_cluster_index_with_no_audio_data: 30,
        cluster_size: 4096,
        cluster_size_magnitude: 12,
        raw_data_format: 0,
        byte_depth: 2,
        num_channels: 2,
    }
}

/// A fill-context set for asset A round-trips through `fill_context_for` unchanged.
#[test]
fn registered_asset_round_trips() {
    let ctx = sample_ctx();
    deluge_streaming_set_fill_context(ptr::null_mut(), 3, ctx);
    let got = fill_context_for(3);
    assert_eq!(got, Some(ctx));
    // byte_depth/num_channels explicitly, on top of the whole-struct equality above: the fill
    // itself never reads them back (`to_fill_geometry` drops them), so this test is their only
    // coverage.
    let got = got.unwrap();
    assert_eq!(got.byte_depth, ctx.byte_depth);
    assert_eq!(got.num_channels, ctx.num_channels);
}

/// An asset id nothing has ever registered reports `None`, not a stale/zeroed context.
#[test]
fn unset_asset_is_none() {
    assert_eq!(fill_context_for(4000), None);
}

/// Re-registering the same asset replaces (not accumulates behind) the prior context — mirrors
/// `open_read_stream()`'s defensive re-registration once the efatfs handle becomes known.
#[test]
fn re_registering_replaces_the_prior_context() {
    let first = sample_ctx();
    deluge_streaming_set_fill_context(ptr::null_mut(), 5, first);
    assert_eq!(fill_context_for(5), Some(first));

    let mut second = first;
    second.efatfs_handle = 99;
    deluge_streaming_set_fill_context(ptr::null_mut(), 5, second);
    assert_eq!(fill_context_for(5), Some(second));
}

/// Out-of-range asset ids (including `DELUGE_RESOURCE_NO_ASSET == u32::MAX`) are silently ignored by
/// the write side and always report `None` on the read side — no out-of-bounds panic either way.
#[test]
fn out_of_range_asset_ids_are_ignored_not_panicking() {
    deluge_streaming_set_fill_context(ptr::null_mut(), u32::MAX, sample_ctx());
    assert_eq!(fill_context_for(u32::MAX), None);
}
