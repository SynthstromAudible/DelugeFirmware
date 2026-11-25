//! The manager-reaching cluster fill: [`native_begin`] resolves a chunk's read, and
//! [`native_finish`] converts, stitches and publishes it.
//!
//! Shared by the BSP's async fill task and the sample reader's synchronous fill. Behind the
//! `native_fill` feature because the resource-manager symbols declared below are defined
//! elsewhere and resolve only in a final link that has them.
use core::ffi::c_void;

use crate::{DelugeChunkConvertState, FillContext, StreamingFillDescriptor, fill_context_for};

unsafe extern "C" {
    pub fn deluge_streaming_resource_manager() -> *mut c_void;
    // The resource imports are `pub` only so pins.rs can pin them; other crates call `deluge_storage_resource`.
    // Recovers a chunk's `(asset, index)` through the out-params; false on a miss.
    pub fn deluge_resource_chunk_ident(
        mgr: *mut c_void,
        ptr: *mut c_void,
        out_asset: *mut u32,
        out_index: *mut u32,
    ) -> bool;
    // `try_acquire` returns a chunk only if it is resident and ready, taking a hard lease on it
    // atomically so it cannot be evicted mid-stitch; `release` drops the lease.
    pub fn deluge_resource_try_acquire(mgr: *mut c_void, asset: u32, index: u32) -> *mut c_void;
    pub fn deluge_resource_release(mgr: *mut c_void, ptr: *mut c_void);
    pub fn deluge_resource_mark_ready(mgr: *mut c_void, ptr: *mut c_void);
}

/// The descriptor for a chunk whose identity, fill context or geometry cannot be resolved.
///
/// Should not happen — an asset registers its context before any of its chunks is queued — but
/// failing closed is safer than reading a missing geometry. Callers skip a `!ok` descriptor: no
/// read, no finish.
const fn geometry_error() -> StreamingFillDescriptor {
    StreamingFillDescriptor {
        dest: core::ptr::null_mut(),
        num_sectors: 0,
        ok: false,
        handle: 0,
        byte_offset: 0,
    }
}

/// Resolves `chunk`'s `(asset, index)` and its asset's fill context, or `None` if either misses.
fn resolve(mgr: *mut c_void, chunk: *mut c_void) -> Option<(u32, u32, FillContext)> {
    let mut asset = 0u32;
    let mut index = 0u32;
    // SAFETY: `mgr` is the live resource manager; `chunk` is a leased chunk (caller contract).
    if !unsafe { deluge_resource_chunk_ident(mgr, chunk, &mut asset, &mut index) } {
        return None;
    }
    let ctx = fill_context_for(asset)?;
    Some((asset, index, ctx))
}

/// Converts the chunk's convert state to [`fill_logic`](crate::fill_logic)'s identical shape.
fn to_logic_state(s: DelugeChunkConvertState) -> crate::fill_logic::ConvertState {
    crate::fill_logic::ConvertState {
        first_three_bytes: s.first_three_bytes,
        start_converted: s.start_converted,
        end_converted: s.end_converted,
    }
}

/// The inverse of [`to_logic_state`].
fn from_logic_state(s: crate::fill_logic::ConvertState) -> DelugeChunkConvertState {
    DelugeChunkConvertState {
        first_three_bytes: s.first_three_bytes,
        start_converted: s.start_converted,
        end_converted: s.end_converted,
    }
}

/// Returns the geometry subset of `ctx` that [`fill_logic`](crate::fill_logic) needs.
fn to_fill_geometry(ctx: &FillContext) -> crate::fill_logic::FillGeometry {
    crate::fill_logic::FillGeometry {
        audio_data_start_pos_bytes: ctx.audio_data_start_pos_bytes,
        audio_data_length_bytes: ctx.audio_data_length_bytes,
        first_cluster_index_with_no_audio_data: ctx.first_cluster_index_with_no_audio_data,
        cluster_size: ctx.cluster_size,
        cluster_size_magnitude: ctx.cluster_size_magnitude,
        raw_data_format: ctx.raw_data_format,
    }
}

/// Resolves `chunk_backing`'s read: its payload destination, sector count, file handle and byte
/// offset. Returns a `!ok` descriptor if the chunk cannot be resolved.
///
/// Uses the process-wide resource manager, so the async fill task and the reader's synchronous
/// fill call it with just the chunk. `chunk_backing` must be a resident chunk the caller holds a
/// lease on; see [`native_finish`] for the context rules.
///
/// # Safety
///
/// `chunk_backing` must be a resident chunk the caller holds a lease on.
pub unsafe fn native_begin(chunk_backing: *mut c_void) -> StreamingFillDescriptor {
    // SAFETY: no preconditions; returns the process-wide resource manager.
    let mgr = unsafe { deluge_streaming_resource_manager() };
    let Some((_asset, index, ctx)) = resolve(mgr, chunk_backing) else {
        return geometry_error();
    };
    let geo = to_fill_geometry(&ctx);
    let r = crate::fill_logic::begin(index, &geo);
    if !r.ok {
        return geometry_error();
    }
    // SAFETY: `chunk_backing` is a resident, leased chunk (caller contract).
    let dest = unsafe { crate::chunk::payload(chunk_backing) };
    StreamingFillDescriptor {
        dest,
        num_sectors: r.num_sectors,
        ok: true,
        handle: ctx.efatfs_handle,
        byte_offset: r.byte_offset,
    }
}

/// Converts, stitches and publishes `chunk_backing` after its read. Returns false if `read_ok` is
/// false or the chunk cannot be resolved.
///
/// Stitches against each neighbour that is resident and ready, leasing it for the duration, then
/// writes back the convert states, marks the chunk loaded and tells the manager it is ready.
///
/// Callable from any context that holds a lease on `chunk_backing`: the manager calls
/// (`try_acquire`, `release`, `mark_ready`) take the manager's own critical section, and the
/// unsynchronized chunk-field accesses touch only chunks this call holds leases on.
///
/// # Safety
///
/// `chunk_backing` must be a resident chunk the caller holds a lease on.
pub unsafe fn native_finish(chunk_backing: *mut c_void, read_ok: bool) -> bool {
    if !read_ok {
        // The reader's synchronous fill passes a failed read through.
        return false;
    }

    // SAFETY: no preconditions; returns the process-wide resource manager.
    let mgr = unsafe { deluge_streaming_resource_manager() };

    let Some((asset, index, ctx)) = resolve(mgr, chunk_backing) else {
        return false;
    };
    let geo = to_fill_geometry(&ctx);
    let payload_len = ctx.cluster_size as usize + 7;

    // SAFETY: `chunk_backing` is a resident, leased chunk whose payload buffer holds
    // `cluster_size + 7` bytes (the trailing slack).
    let self_payload = unsafe {
        core::slice::from_raw_parts_mut(crate::chunk::payload(chunk_backing), payload_len)
    };
    // SAFETY: `chunk_backing` is a resident, leased chunk.
    let mut self_state = to_logic_state(unsafe { crate::chunk::convert_state(chunk_backing) });

    // Gather each neighbour with `try_acquire`, which checks resident-and-ready and leases in one
    // step, so eviction cannot race a separate check. The leases pin the neighbours' payloads only
    // until the stitch is done.
    let mut prev_lease: Option<*mut c_void> = None;
    let mut prev_state = crate::fill_logic::ConvertState::default();
    if let Some(prev_index) = index.checked_sub(1) {
        // SAFETY: `mgr` is the live manager; the rest is a plain lookup.
        let p = unsafe { deluge_resource_try_acquire(mgr, asset, prev_index) };
        if !p.is_null() {
            // SAFETY: `try_acquire` just leased `p` and found it resident.
            prev_state = to_logic_state(unsafe { crate::chunk::convert_state(p) });
            prev_lease = Some(p);
        }
    }
    let prev_view = prev_lease.map(|p| crate::fill_logic::NeighbourView {
        // SAFETY: `p` is a leased, resident chunk backing; its payload buffer holds
        // `cluster_size + 7` bytes, matching `payload_len`.
        payload: unsafe { core::slice::from_raw_parts_mut(crate::chunk::payload(p), payload_len) },
        unconverted_head: &prev_state.first_three_bytes,
        start_converted: &mut prev_state.start_converted,
        end_converted: &mut prev_state.end_converted,
    });

    let mut next_lease: Option<*mut c_void> = None;
    let mut next_state = crate::fill_logic::ConvertState::default();
    if let Some(next_index) = index.checked_add(1) {
        // SAFETY: `mgr` is the live manager; the rest is a plain lookup.
        let p = unsafe { deluge_resource_try_acquire(mgr, asset, next_index) };
        if !p.is_null() {
            // SAFETY: `try_acquire` just leased `p` and found it resident.
            next_state = to_logic_state(unsafe { crate::chunk::convert_state(p) });
            next_lease = Some(p);
        }
    }
    let next_view = next_lease.map(|p| crate::fill_logic::NeighbourView {
        // SAFETY: same as the prev branch above.
        payload: unsafe { core::slice::from_raw_parts_mut(crate::chunk::payload(p), payload_len) },
        unconverted_head: &next_state.first_three_bytes,
        start_converted: &mut next_state.start_converted,
        end_converted: &mut next_state.end_converted,
    });

    crate::fill_logic::finish_convert_stitch(
        self_payload,
        index,
        &geo,
        &mut self_state,
        prev_view,
        next_view,
    );

    fold_peaks(
        asset,
        index,
        &ctx,
        (chunk_backing, self_state),
        prev_lease.map(|p| (p, prev_state)),
        next_lease.map(|p| (p, next_state)),
    );

    // Write back the convert states, then release the neighbour leases.
    // SAFETY: `chunk_backing` is still the leased chunk.
    unsafe { crate::chunk::set_convert_state(chunk_backing, from_logic_state(self_state)) };
    if let Some(p) = prev_lease {
        // SAFETY: `p` is still leased and resident.
        unsafe { crate::chunk::set_convert_state(p, from_logic_state(prev_state)) };
        // SAFETY: `p` is leased by this call and released exactly once.
        unsafe { deluge_resource_release(mgr, p) };
    }
    if let Some(p) = next_lease {
        // SAFETY: same as the prev write-back above.
        unsafe { crate::chunk::set_convert_state(p, from_logic_state(next_state)) };
        // SAFETY: same as the prev release above.
        unsafe { deluge_resource_release(mgr, p) };
    }

    // SAFETY: `chunk_backing` is still the leased chunk.
    unsafe { crate::chunk::set_loaded(chunk_backing) };
    // Publish readiness to the manager, so `try_acquire` from any tier sees the chunk ready.
    // SAFETY: `mgr` and `chunk_backing` are both still valid.
    unsafe { deluge_resource_mark_ready(mgr, chunk_backing) };
    true
}

/// Folds the peaks of the blocks the just-filled cluster `index` (and its leased neighbours) holds into the index bound
/// to `asset`, if one is.
///
/// A payload's bytes count as converted audio as follows. A native file needs no convert. Otherwise a boundary word
/// straddling two clusters is converted only by a stitch with both present: a cluster's head word once its previous
/// cluster has been present (this stitch, or `start_converted`), its tail word once its next has (this stitch, or
/// `end_converted`). The first audio cluster has no head word to wait for and the last no tail word. The slack holds
/// the next cluster's converted head only when this stitch wrote it.
fn fold_peaks(
    asset: u32,
    index: u32,
    ctx: &crate::FillContext,
    this: (*mut c_void, crate::fill_logic::ConvertState),
    prev: Option<(*mut c_void, crate::fill_logic::ConvertState)>,
    next: Option<(*mut c_void, crate::fill_logic::ConvertState)>,
) {
    use deluge_storage_sample_peaks::tap::{ClusterGeometry, ClusterView, fold_cluster, lookup};
    let Some(peaks) = lookup(asset) else {
        return;
    };
    let cs = ctx.cluster_size;
    let payload_len = cs as usize + 7;
    let native = ctx.raw_data_format == 0;
    let first_audio = ctx.audio_data_start_pos_bytes >> ctx.cluster_size_magnitude;
    let last_audio = u32::try_from(ctx.first_cluster_index_with_no_audio_data)
        .unwrap_or(0)
        .saturating_sub(1);
    let head_from = |c: u32, converted: bool| {
        if native || converted || c == first_audio {
            0
        } else {
            4
        }
    };
    let tail_end = |c: u32, converted: bool| {
        if native || converted || c >= last_audio {
            cs
        } else {
            cs - 4
        }
    };
    let view = |p: *mut c_void, c: u32, valid_from: u32, valid_end: u32| ClusterView {
        index: c,
        // SAFETY: `p` is a leased, resident chunk backing whose payload holds `cluster_size + 7` bytes; the view is
        // read only while the lease is held, before the caller releases it.
        payload: unsafe { core::slice::from_raw_parts(crate::chunk::payload(p), payload_len) },
        valid_from,
        valid_end,
    };
    let geo = ClusterGeometry {
        audio_start: u64::from(ctx.audio_data_start_pos_bytes),
        audio_length: ctx.audio_data_length_bytes,
        cluster_size_log2: ctx.cluster_size_magnitude,
        byte_depth: ctx.byte_depth,
        channels: ctx.num_channels,
    };
    // This stitch converted this cluster's head (with `prev`) and tail and slack (with `next`), and `prev`'s tail and
    // slack, and `next`'s head.
    let this_view = view(
        this.0,
        index,
        if prev.is_some() {
            0
        } else {
            head_from(index, this.1.start_converted)
        },
        if next.is_some() {
            cs + 7
        } else {
            tail_end(index, this.1.end_converted)
        },
    );
    let prev_view = prev.map(|(p, st)| {
        view(
            p,
            index - 1,
            head_from(index - 1, st.start_converted),
            cs + 7,
        )
    });
    let next_view =
        next.map(|(p, st)| view(p, index + 1, 0, tail_end(index + 1, st.end_converted)));
    // SAFETY: `peaks` holds the reference `lookup` took until the release below.
    fold_cluster(
        unsafe { peaks.as_ref() },
        &geo,
        prev_view,
        this_view,
        next_view,
    );
    deluge_storage_sample_peaks::PeakIndex::release(peaks);
}
