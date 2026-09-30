//! The manager-reaching cluster-fill core, shared by the Rust BSP's async fill task
//! (`ProdOps::begin`/`finish`, which links the `native_begin`/`native_finish` implementation here)
//! and the range reader's synchronous fill path. Gated behind the `native_fill`
//! feature (default-off): every `unsafe extern "C" { … }` symbol below is a resource-manager
//! accessor this crate itself does not define, so this module only compiles where a final link —
//! device, `host_app`, or a test harness supplying the real symbols (`region_fill_differential`) —
//! will actually resolve them (see this crate's `Cargo.toml` `[features]` doc). The `StreamedChunk`
//! field accessors this module also uses (`payload`/`set_loaded`/`convert_state`/
//! `set_convert_state`) are `pub fn`s this crate DOES define, in [`crate::chunk`] — called directly,
//! no C-ABI hop.
use core::ffi::c_void;

use crate::{DelugeChunkConvertState, FillContext, StreamingFillDescriptor, fill_context_for};

unsafe extern "C" {
    pub fn deluge_streaming_resource_manager() -> *mut c_void;
    // `deluge_resource_chunk_ident`: recovers a loader-queue chunk's `(asset, index)` identity
    // (out-params; `false` on a miss) so `begin`/`finish` can look up its per-asset fill-context
    // (`fill_context_for`) -- mirrors `deluge_resource_slot_of` (declared in `deluge-bsp-rust`'s own
    // `streaming_loader.rs`, used by `ProdOps`) for the loader-queue chunk this native core also
    // serves.
    fn deluge_resource_chunk_ident(
        mgr: *mut c_void,
        ptr: *mut c_void,
        out_asset: *mut u32,
        out_index: *mut u32,
    ) -> bool;
    // `deluge_resource_try_acquire`/`_release`: the neighbour-lease safety mechanism `finish`
    // uses to gather a stitch neighbour (see its doc) -- `try_acquire` is the SAME "resident AND
    // ready" check `prevCluster && prevCluster->loaded` performs in the C++, but atomically also
    // takes a hard lease on a hit (so the neighbour can't be evicted out from under the stitch);
    // `release` drops that lease again once the stitch is done.
    fn deluge_resource_try_acquire(mgr: *mut c_void, asset: u32, index: u32) -> *mut u8;
    fn deluge_resource_release(mgr: *mut c_void, ptr: *mut u8);
    fn deluge_resource_mark_ready(mgr: *mut c_void, ptr: *mut c_void);
}

/// The "geometry error / not resolvable" `StreamingFillDescriptor` -- mirrors `begin_fill`'s own
/// early-return literally (`dest: nullptr, num_sectors: 0, ok: false, handle: 0, byte_offset: 0`,
/// `async_fill.cpp:89-90`). `ProdOps::begin` returns this whenever a chunk's identity or
/// fill-context can't be resolved -- should not happen in practice (every chunk on the loader
/// queue is a resident, still-leased `StreamedChunk` whose asset registered its context at
/// `deluge_streaming_define_asset()` before it could ever be enqueued -- see `streaming_loader.rs`'s
/// module doc), but failing closed here is strictly safer than dereferencing a geometry that isn't
/// there. `fill_once` already treats `!d.ok` as "skip this chunk, don't read, don't call finish" (the
/// same path an unloadable/geometry-error chunk already takes), so this degrades exactly like that
/// existing, tested case.
const fn geometry_error() -> StreamingFillDescriptor {
    StreamingFillDescriptor {
        dest: core::ptr::null_mut(),
        num_sectors: 0,
        ok: false,
        handle: 0,
        byte_offset: 0,
    }
}

/// Resolve `chunk`'s `(asset, index)` identity and its asset's registered fill-context, or
/// `None` if either lookup misses (see [`geometry_error`]'s doc for why that "shouldn't really
/// still happen" but is handled anyway).
fn resolve(mgr: *mut c_void, chunk: *mut c_void) -> Option<(u32, u32, FillContext)> {
    let mut asset = 0u32;
    let mut index = 0u32;
    // SAFETY: `mgr` is the live singleton resource manager; `chunk` is a still-leased
    // `StreamedChunk*` the caller is already treating as valid for this call.
    if !unsafe { deluge_resource_chunk_ident(mgr, chunk, &mut asset, &mut index) } {
        return None;
    }
    let ctx = fill_context_for(asset)?;
    Some((asset, index, ctx))
}

/// `DelugeChunkConvertState` (the type [`crate::chunk::convert_state`]/[`crate::chunk::set_convert_state`]
/// read/write) -> `fill_logic::ConvertState`: the two are separate types with the
/// identical three-field shape (see `fill_logic::ConvertState`'s doc for why they aren't the same
/// type) -- a trivial field-for-field copy at the one tier where both exist.
fn to_logic_state(s: DelugeChunkConvertState) -> crate::fill_logic::ConvertState {
    crate::fill_logic::ConvertState {
        first_three_bytes: s.first_three_bytes,
        start_converted: s.start_converted,
        end_converted: s.end_converted,
    }
}

/// The inverse of [`to_logic_state`], for writing `finish_convert_stitch`'s (possibly updated)
/// output back onto the `StreamedChunk` via [`crate::chunk::set_convert_state`].
fn from_logic_state(s: crate::fill_logic::ConvertState) -> DelugeChunkConvertState {
    DelugeChunkConvertState {
        first_three_bytes: s.first_three_bytes,
        start_converted: s.start_converted,
        end_converted: s.end_converted,
    }
}

/// `FillContext` (the C-ABI-mirroring registration record) -> `FillGeometry` (`fill_logic`'s
/// pure-arithmetic input) -- a plain field subset (drops `efatfs_handle`, which `begin` threads
/// through separately into the descriptor's own `handle` field, not through the geometry).
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

/// Resolve `chunk_backing`'s destination buffer + physical sector range
/// (`deluge_streaming_begin_fill`'s native replacement, factored into a free function).
/// Looks up the live singleton resource manager itself (`deluge_streaming_resource_manager`)
/// rather than taking `mgr` as a parameter: there is exactly one process-wide manager, and this
/// shape lets `ProdOps::begin` (the async fill task, in `deluge-bsp-rust`'s `streaming_loader.rs`)
/// and the range reader's synchronous `fill_now` (`deluge_sample_reader`) call the SAME function with
/// nothing but the chunk pointer. `chunk_backing` must be a queued (or, from the sync path, otherwise
/// still-leased), resident `StreamedChunk*` -- see the module doc's
/// "Sync-context safety" note on [`native_finish`] below, which applies identically here (this
/// function touches strictly less state: no neighbour gather, no convert-state read/write).
// `chunk_backing`'s validity is a precondition the caller already upholds (a still-leased
// `StreamedChunk*` from either the loader queue or the sync C++ fill path -- see the doc above).
// This function's `pub` signature is fixed by this crate's C-ABI-facing shape, so its raw-pointer
// dereference (via the `unsafe extern "C"` calls below) is silenced rather than changed to
// `unsafe fn`.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub fn native_begin(chunk_backing: *mut c_void) -> StreamingFillDescriptor {
    // SAFETY: returns the one process-wide GeneralMemoryAllocator resource manager; a stable
    // singleton pointer, no aliasing/ownership concern.
    let mgr = unsafe { deluge_streaming_resource_manager() };
    let Some((_asset, index, ctx)) = resolve(mgr, chunk_backing) else {
        return geometry_error();
    };
    let geo = to_fill_geometry(&ctx);
    let r = crate::fill_logic::begin(index, &geo);
    if !r.ok {
        return geometry_error();
    }
    // SAFETY: `chunk_backing` is the same resident, leased `StreamedChunk*` `resolve` just
    // validated has a registered fill-context.
    let dest = unsafe { crate::chunk::payload(chunk_backing) };
    StreamingFillDescriptor {
        dest,
        num_sectors: r.num_sectors,
        ok: true,
        handle: ctx.efatfs_handle,
        byte_offset: r.byte_offset,
    }
}

/// Run the post-read convert/stitch/publish tail for `chunk_backing`
/// (`deluge_streaming_finish_fill`'s native replacement, factored into a free function
/// operating on the `StreamedChunk` convert-state accessors). `read_ok` mirrors
/// `finish_fill`'s own early-out contract (see the body below); only called with `true` from
/// `fill_once`'s current calling convention.
///
/// Looks up the live singleton resource manager itself, exactly like [`native_begin`] — see that
/// function's doc for why (both are callable from the async task AND from the range reader's
/// synchronous `fill_now`, in `deluge_sample_reader`).
///
/// ## Sync-context safety
///
/// Every operation this function performs beyond plain arithmetic goes through one of three
/// primitives, and all three are already relied on from a SYNCHRONOUS caller elsewhere in this
/// codebase, not just this async task:
/// - [`deluge_resource_try_acquire`]/[`deluge_resource_release`]: the manager's own masked
///   critical section (`deluge_resource::sync::Masked`) guards every table mutation these make,
///   the same masking the C++ side's own manager calls (`deluge_resource_request`,
///   `deluge_resource_release`, etc. — see `sample_stream.cpp`) already go through. Nothing
///   about calling them from a synchronous, non-async context is new.
/// - [`crate::chunk::convert_state`]/[`crate::chunk::set_convert_state`]: plain field reads/writes
///   on a `StreamedChunk*` (see `chunk.rs`'s definitions) — no locking at all, by design, exactly
///   like the pre-existing [`crate::chunk::payload`]/[`crate::chunk::set_loaded`] this function
///   already called before this task. Safe
///   because the chunk this function touches (`chunk_backing` itself, and each neighbour just
///   after its own successful `try_acquire`) is hard-leased for the duration of this call — the
///   SAME "leased, so exclusively mine to mutate until I release it" discipline the range reader's
///   synchronous `fill_now` (`deluge_sample_reader`) relies on when it calls this function to write
///   these same fields directly. A synchronous caller on the single thread-mode executor (that
///   reader path never runs on the audio render ISR either — see `streaming_loader`'s module doc) has
///   the identical exclusivity guarantee this async task has today.
/// - [`deluge_resource_mark_ready`]: also masked inside the manager, same as the acquire/release
///   pair above.
///
/// In short: nothing this function does depends on running on the Embassy executor specifically —
/// it depends only on the caller already holding a lease on `chunk_backing` (true for both the
/// loader-queue chunk this async task pops and the chunk the sync C++ path would already be
/// holding a lease on to call this at all) and on the manager's existing masked-critical-section
/// discipline, which is unconditional regardless of caller. No new cross-context hazard was found.
// See `native_begin`'s own `#[allow]` just above for why this raw-pointer dereference is silenced
// rather than expressed as `unsafe fn`.
#[allow(clippy::not_unsafe_ptr_arg_deref)]
pub fn native_finish(chunk_backing: *mut c_void, read_ok: bool) -> bool {
    if !read_ok {
        // Mirrors `finish_fill`'s own `if (!read_ok) { return false; }` early-out
        // (`async_fill.cpp:107-110`) -- dead in practice under `fill_once`'s current
        // calling convention (it only ever calls `finish` after `read` succeeds; see
        // `fill_once`'s doc), kept for parity with the upcall's contract this replaces.
        return false;
    }

    // SAFETY: returns the one process-wide GeneralMemoryAllocator resource manager; a stable
    // singleton pointer, no aliasing/ownership concern.
    let mgr = unsafe { deluge_streaming_resource_manager() };

    // Native `finish` -- convert + stitch + publish. `chunk_backing` is the same pointer `begin`
    // was just called with, whose data has just been successfully read into its payload buffer.
    let Some((asset, index, ctx)) = resolve(mgr, chunk_backing) else {
        return false;
    };
    let geo = to_fill_geometry(&ctx);
    let payload_len = ctx.cluster_size as usize + 7;

    // SAFETY: `chunk_backing` is a resident, still-leased `StreamedChunk*`; its payload buffer is
    // `payload_with_trailing_slack()` -- `cluster_size + 7` bytes, matching `payload_len`.
    let self_payload = unsafe {
        core::slice::from_raw_parts_mut(crate::chunk::payload(chunk_backing), payload_len)
    };
    // SAFETY: `chunk_backing` is a resident, still-leased `StreamedChunk*` (see this function's
    // doc, "Sync-context safety" above).
    let mut self_state = to_logic_state(unsafe { crate::chunk::convert_state(chunk_backing) });

    // Gather each neighbour, mirroring `async_fill.cpp:116-157`'s "present AND loaded" gate
    // in one step: `try_acquire` reports resident-and-ready (the manager's `Loading` state
    // is exactly the not-yet-`loaded` case the C++ checks) AND atomically takes a hard lease
    // on a hit, so the neighbour can't be evicted out from under the stitch below the way a
    // separate check-then-touch could race against an eviction between the two. The lease is
    // released again right after the stitch (see the bottom of this function) -- it exists
    // purely to pin the neighbour's payload buffer alive for this call's duration, not to
    // hold it any longer than that (mirroring the C++'s use-then-forget borrow of
    // `prevCluster`/`nextCluster`, just made eviction-safe under the manager's real
    // lease/evict machinery, which the synchronous C++ path never had to contend with).
    let mut prev_lease: Option<*mut u8> = None;
    let mut prev_state = crate::fill_logic::ConvertState::default();
    if let Some(prev_index) = index.checked_sub(1) {
        // SAFETY: `mgr` is the live manager; `asset`/`prev_index` are a plain lookup.
        let p = unsafe { deluge_resource_try_acquire(mgr, asset, prev_index) };
        if !p.is_null() {
            // SAFETY: `p` was just leased+validated resident by `try_acquire` above.
            prev_state = to_logic_state(unsafe { crate::chunk::convert_state(p as *mut c_void) });
            prev_lease = Some(p);
        }
    }
    let prev_view = prev_lease.map(|p| crate::fill_logic::NeighbourView {
        // SAFETY: `p` was just leased+validated resident by `try_acquire` above, but `p`
        // itself is the neighbour's BACKING pointer (`== StreamedChunk*`), not its payload --
        // same distinction as `chunk_backing` vs `self_payload` above.
        // `crate::chunk::payload(p)` returns the neighbour's payload base
        // (`backing + kChunkPayloadOffset`), its `payload_with_trailing_slack()` buffer --
        // `cluster_size + 7` bytes, matching `payload_len`.
        payload: unsafe {
            core::slice::from_raw_parts_mut(crate::chunk::payload(p as *mut c_void), payload_len)
        },
        unconverted_head: &prev_state.first_three_bytes,
        start_converted: &mut prev_state.start_converted,
        end_converted: &mut prev_state.end_converted,
    });

    let mut next_lease: Option<*mut u8> = None;
    let mut next_state = crate::fill_logic::ConvertState::default();
    if let Some(next_index) = index.checked_add(1) {
        // SAFETY: `mgr` is the live manager; `asset`/`next_index` are a plain lookup.
        let p = unsafe { deluge_resource_try_acquire(mgr, asset, next_index) };
        if !p.is_null() {
            // SAFETY: `p` was just leased+validated resident by `try_acquire` above.
            next_state = to_logic_state(unsafe { crate::chunk::convert_state(p as *mut c_void) });
            next_lease = Some(p);
        }
    }
    let next_view = next_lease.map(|p| crate::fill_logic::NeighbourView {
        // SAFETY: same as the prev branch above.
        payload: unsafe {
            core::slice::from_raw_parts_mut(crate::chunk::payload(p as *mut c_void), payload_len)
        },
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

    // Write back the (possibly updated) convert-state -- self, and each neighbour actually
    // visited -- then release the neighbour leases taken above.
    // SAFETY: `chunk_backing` is still the valid, leased `StreamedChunk*` from `next()`.
    unsafe { crate::chunk::set_convert_state(chunk_backing, from_logic_state(self_state)) };
    if let Some(p) = prev_lease {
        // SAFETY: `p` was leased by `deluge_resource_try_acquire` above and is still resident.
        unsafe { crate::chunk::set_convert_state(p as *mut c_void, from_logic_state(prev_state)) };
        // SAFETY: `p` was leased by `deluge_resource_try_acquire` above; released exactly
        // once, now that the stitch that needed it pinned alive is done.
        unsafe { deluge_resource_release(mgr, p) };
    }
    if let Some(p) = next_lease {
        // SAFETY: same as the prev write-back above.
        unsafe { crate::chunk::set_convert_state(p as *mut c_void, from_logic_state(next_state)) };
        // SAFETY: same as the prev release above.
        unsafe { deluge_resource_release(mgr, p) };
    }

    // SAFETY: `chunk_backing` is still the valid, leased `StreamedChunk*` from `next()`.
    unsafe { crate::chunk::set_loaded(chunk_backing) };
    // Manager-owned readiness: mirrors `finish_fill`'s own `deluge_resource_mark_ready`
    // call (`async_fill.cpp:169-173`) so the async/RT `try_acquire` path sees this chunk
    // ready.
    // SAFETY: `mgr`/`chunk_backing` are both still valid.
    unsafe { deluge_resource_mark_ready(mgr, chunk_backing) };
    true
}
