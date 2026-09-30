//! Tests the per-asset fill-context table: `deluge_streaming_set_fill_context` writes, and
//! `fill_context_for` reads back.
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
    // Checked explicitly because the fill never reads these two fields.
    let got = got.unwrap();
    assert_eq!(got.byte_depth, ctx.byte_depth);
    assert_eq!(got.num_channels, ctx.num_channels);
}

/// An asset id nothing has ever registered reports `None`, not a stale/zeroed context.
#[test]
fn unset_asset_is_none() {
    assert_eq!(fill_context_for(4000), None);
}

/// Re-registering replaces the prior context, as happens once a sample's file handle is known.
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

/// Out-of-range ids, including `DELUGE_RESOURCE_NO_ASSET` (`u32::MAX`), are ignored on write and
/// read back as `None`, without panicking.
#[test]
fn out_of_range_asset_ids_are_ignored_not_panicking() {
    deluge_streaming_set_fill_context(ptr::null_mut(), u32::MAX, sample_ctx());
    assert_eq!(fill_context_for(u32::MAX), None);
}
