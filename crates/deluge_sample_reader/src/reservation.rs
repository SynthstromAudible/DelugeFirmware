//! The passive lookahead reservation handle (`include/libdeluge/sample_reader.h`'s
//! `DelugeSampleReservation`/`deluge_sample_reserve_open`/`_close`) — the ACTIVE twin of
//! [`crate::reader::peek`]. Where a peek is a single, transient, non-pinning glance at whatever is
//! already resident, a reservation pins a small forward- or backward-facing WINDOW of cluster
//! residency (one real `deluge_resource::facade::Lease` per covered cluster) for as long as it
//! stays open — the same shape the existing `kNumClustersLoadedAhead` (`definitions_cxx.hpp`)
//! lookahead pins already give the timestretch/loop-point paths, generalized behind this crate's
//! own C-ABI handle.
//!
//! `open`/`close`/`covered_indices` provide the coverage + lease accounting.
//! [`Reservation::reanchor`] slides the window as playback advances, guarded against
//! per-render-tick lease churn when the marker drifts within its current head cluster.
//! Synchronous load-now materialization for [`LoadMode::Now`]/[`LoadMode::NowOrEnqueue`] is
//! provided too — see [`load_cluster`]'s own doc for the per-mode recipe.

use alloc::boxed::Box;
use core::ffi::c_void;

use deluge_resource::facade::{Lease, Resource};
use deluge_resource::DelugeResource;

/// How many clusters ahead of (or behind, in reverse) the marker cluster a reservation pins —
/// `kNumClustersLoadedAhead`'s own value (`definitions_cxx.hpp:659`), re-declared here as a plain
/// Rust `const` rather than read across the FFI: the two are independent constants, one per side of
/// the port, matching this crate's `reader.rs`'s own "each side re-derives the one constant it
/// needs" convention (see `Geometry`'s doc there).
const DEPTH: usize = 2;

/// Mirrors `include/libdeluge/sample_reader.h`'s `DelugeLoadMode`. Crosses the FFI by value
/// (`deluge_sample_reserve_open`'s parameter), so `#[repr(u8)]` (fixed width) exactly matches the
/// header's explicit `: uint8_t` — the same fixed-width need `reader::ReadHint` documents for its
/// own enum.
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum LoadMode {
    /// Reserve the covered clusters and enqueue them for the async loader; never blocks.
    Enqueue = 0,
    /// Materialize the covered clusters synchronously before returning. A cluster whose
    /// synchronous fill fails has its reservation released (see [`load_cluster`]'s own doc).
    Now = 1,
    /// Prefer a synchronous load, falling back to enqueueing (keeping the reservation, unfilled)
    /// under memory pressure — mirrors `CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE`.
    NowOrEnqueue = 2,
}

unsafe extern "C" {
    /// The single process-wide resource-manager singleton (`include/libdeluge/sample_stream.h`).
    /// The SAME extern declaration `reader.rs` makes of its own copy — a foreign-fn prototype may be
    /// declared more than once across a crate's modules without conflict (unlike a `#[no_mangle]`
    /// definition, which may only exist once); duplicated here rather than exposed from `reader.rs`
    /// so this module stays independent of `reader.rs`'s own internals.
    pub fn deluge_streaming_resource_manager() -> *mut c_void;
    /// The synchronous card read (`include/libdeluge/file_io.h`) — see [`fill_now`].
    pub fn deluge_efatfs_read_at(
        handle: deluge_sample_fill::DelugeHandle,
        byte_offset: u32,
        dst: *mut c_void,
        count: u32,
        out_read: *mut u32,
    ) -> deluge_sample_fill::DelugeStatus;
    /// Wake the async loader (`include/libdeluge/streaming_fill.h`) after enqueueing a chunk
    /// via `Resource::loader_enqueue` — see [`enqueue_unfilled`]'s own doc for the recipe this
    /// mirrors (the C++ `CLUSTER_ENQUEUE` path).
    pub fn deluge_streaming_signal_fill();
}

/// Run the synchronous cluster fill on `chunk_backing`: resolve where/how much to read
/// (`native_begin`), read exactly that span in ONE `deluge_efatfs_read_at` call, then run the
/// post-read convert/stitch/publish tail (`native_finish`). `deluge-bsp-rust`'s async fill task
/// composes the same two primitives around an `.await`ed read. Returns `native_finish`'s own
/// result: `false` on a geometry-resolution failure, a failed/short read, or a failed
/// convert/stitch/publish; `true` once the chunk is converted, stitched, and published ready.
pub(crate) fn fill_now(chunk_backing: *mut c_void) -> bool {
    let d = deluge_sample_fill::native_begin(chunk_backing);
    if !d.ok {
        return false;
    }
    let count = d.num_sectors * 512;
    let mut out_read: u32 = 0;
    // SAFETY: `d.dest` is this chunk's just-allocated payload buffer (resolved by `native_begin`
    // from the same registered geometry that sized this chunk's backing), valid for at least
    // `count` bytes; `out_read` is a valid local out-param.
    let read_ok = unsafe {
        deluge_efatfs_read_at(
            d.handle,
            d.byte_offset,
            d.dest as *mut c_void,
            count,
            &mut out_read,
        )
    } == deluge_sample_fill::DELUGE_OK;
    deluge_sample_fill::native_finish(chunk_backing, read_ok)
}

/// A passive lookahead reservation over one sample's source residency — the Rust-side state behind
/// the opaque `DelugeSampleReservation` handle.
pub struct Reservation {
    /// The resource-manager Asset id this reservation covers (the header's `source_id`; see
    /// `reader::Reader::open`'s own doc for why this is the SAME id the voice port's residency
    /// uses). Read by [`Reservation::reanchor`] to re-resolve geometry against the new marker.
    asset: u32,
    /// The resolved process-wide resource-manager handle, or null if none was available at `open()`
    /// time (mirrors `reader::Reader::open`'s own `manager` field) — kept even on an otherwise inert
    /// (no-geometry) open, so [`Reservation::reanchor`] can reuse it directly rather than re-querying
    /// the singleton, and can still resolve geometry itself once it exists.
    manager: *mut c_void,
    /// The cluster index `marker_frame` mapped to at `open()` (or the most recent `reanchor()`)
    /// time, or `None` if this reservation is inert (null manager / no registered geometry /
    /// malformed geometry) — distinct from `covered_indices()` being empty, which can also happen
    /// for a live reservation whose head cluster itself sits outside the sample's real audio-data
    /// range. [`Reservation::reanchor`]'s own guard reads this to decide whether a move is a no-op.
    head_index: Option<u32>,
    /// The cluster indices this reservation covers, in WALK order (marker-then-outward, per
    /// `direction`) — up to [`DEPTH`] of them, clamped to the sample's real audio-data cluster
    /// range. Index-aligned with `leases` (`covered[i]` is the cluster `leases[i]` was taken for,
    /// whether or not that particular lease attempt actually succeeded).
    covered: [u32; DEPTH],
    /// How many of `covered`'s slots are valid — `covered_indices()`'s own length.
    num_covered: usize,
    /// One held lease per covered cluster, `None` where the load attempt for that cluster failed
    /// (OOM / a full table with nothing evictable / a construct-less asset) — a failed load does not
    /// shrink `covered_indices()`; it only leaves that slot's own pin absent. Dropping `Reservation`
    /// drops every `Some` here, releasing each held lease exactly once (ordinary `Drop`, no custom
    /// `impl Drop` needed — mirrors `reader::Reader::held_lease`'s own reliance on structural drop).
    /// [`Reservation::reanchor`] slides this array's contents in and out as the window shifts,
    /// releasing every held lease (structural drop, via reassignment) before acquiring any new one.
    leases: [Option<Lease>; DEPTH],
}

impl Reservation {
    /// Open a reservation over `asset`, pinning up to [`DEPTH`] clusters starting at the cluster
    /// `marker_frame` maps to, walking in `direction` (+1 forward, -1 reverse).
    ///
    /// # Geometry resolution — mirrors `peek` exactly
    /// Resolves the manager (`deluge_streaming_resource_manager`) and this asset's registered
    /// geometry (`deluge_sample_fill::fill_context_for`) the SAME way [`crate::reader::peek`] does:
    /// a null manager, an unregistered asset, or a malformed geometry (zero frame stride) all
    /// produce an INERT reservation — no leases taken, `head_index = None` — exactly as `peek`
    /// returns null in those same cases. `asset` here is `source_id`: the SAME resource-manager
    /// Asset id `reader::Reader::open`/`peek` already resolve geometry from (see either's own doc).
    ///
    /// # Coverage: clamped, marker-then-outward walk
    /// `start_byte = audio_data_start_pos_bytes + marker_frame * frame_stride`,
    /// `head = start_byte >> cluster_size_magnitude` (the registered `FillContext`'s own shift, not
    /// a re-derived division — this reservation reads the raw `FillContext` fields directly rather
    /// than going through `reader::Reader`'s private `Geometry` mirror, which does not carry
    /// `cluster_size_magnitude`/`first_cluster_index_with_no_audio_data`; see this module's own
    /// doc for why `reader.rs` stays untouched). From `head`, up to `DEPTH` cluster indices are
    /// walked in `direction`, stopping the walk (not just omitting the one out-of-range index) the
    /// moment an index falls outside `[first_with_data, first_no_data)`: `first_with_data` is the
    /// cluster containing `audio_data_start_pos_bytes` itself (`>> cluster_size_magnitude`);
    /// `first_no_data` is the registered `first_cluster_index_with_no_audio_data`, or "no bound" if
    /// that field is negative (the sentinel a still-recording/not-yet-finalized sample's context
    /// uses — see `reader.rs`'s own `lifecycle_fill_context` for the same sentinel value, `-1`).
    ///
    /// # Load recipe per covered cluster
    /// See [`load_cluster`]'s own doc for the full per-`load_mode` recipe (cache hit vs. reserve
    /// vs. reserve-and-materialize). A load failure (OOM, nothing evictable, a construct-less
    /// asset, or — for `LoadMode::Now` — a failed synchronous fill) leaves that slot's own lease
    /// `None` but does NOT remove its index from `covered_indices()` — the covered RANGE is a
    /// purely geometric fact, independent of whether a particular lease attempt happened to
    /// succeed.
    ///
    /// `LoadMode::Now`/`LoadMode::NowOrEnqueue` BLOCK this call (and [`Reservation::reanchor`]) on
    /// the synchronous fill of every covered cluster that isn't already resident+ready —
    /// `LoadMode::Enqueue` never blocks.
    pub fn open(asset: u32, marker_frame: u64, direction: i8, load_mode: LoadMode) -> Reservation {
        // SAFETY: returns the one process-wide resource-manager singleton; a stable pointer, no
        // aliasing/ownership concern — same call, same contract as `reader::Reader::open`'s own use
        // of this extern.
        let manager = unsafe { deluge_streaming_resource_manager() };
        let Some(geometry) = resolve_geometry(manager, asset, marker_frame) else {
            return Reservation {
                asset,
                manager,
                head_index: None,
                covered: [0; DEPTH],
                num_covered: 0,
                leases: core::array::from_fn(|_| None),
            };
        };

        let (covered, leases, num_covered) =
            walk_and_lease(manager, asset, &geometry, direction, load_mode);
        Reservation {
            asset,
            manager,
            head_index: Some(geometry.head),
            covered,
            num_covered,
            leases,
        }
    }

    /// Re-anchor this reservation to the cluster containing `marker_frame`, walking in `direction`
    /// exactly as [`Reservation::open`] does — the header's `deluge_sample_reserve_move`.
    ///
    /// # The guard: same head cluster is a no-op
    /// Resolves the new head cluster the SAME way `open` resolves its own (see that method's own
    /// doc for the full geometry-resolution recipe). If the newly resolved head cluster is the SAME
    /// one this reservation is already anchored on (`Some(new_head) == self.head_index`), this
    /// returns immediately WITHOUT touching a single lease — no release, no acquire. This is the
    /// guard that keeps a marker drifting within its current head cluster (the common case, once per
    /// render tick) from repeatedly releasing and re-acquiring the SAME leases.
    ///
    /// # Otherwise: release-old-then-acquire-new
    /// Every lease this reservation currently holds is released FIRST (reassigning `leases` drops
    /// the old array's contents structurally), THEN the covered window is rebuilt from the new head
    /// via the exact same walk-and-lease recipe `open` uses. This order — not
    /// acquire-new-before-release-old — matches the C++ path this ports: releasing old pins before
    /// taking new ones preserves eviction-recency fidelity (a stale-but-still-held pin must not keep
    /// an LRU-eligible slot artificially warm past the point this reservation actually needs it).
    ///
    /// A resolution failure (manager still unavailable, geometry now missing/malformed) rebuilds
    /// into the same inert shape `open` itself returns for those cases — old leases released, no new
    /// ones taken.
    pub fn reanchor(&mut self, asset: u32, marker_frame: u64, direction: i8, load_mode: LoadMode) {
        // Reuse the manager resolved at `open()` time when we have one (the common case — see the
        // `manager` field's own doc); otherwise retry the singleton lookup, since geometry may have
        // become resolvable since (e.g. streaming boot completing after this reservation opened).
        let manager = if self.manager.is_null() {
            // SAFETY: same call, same contract as `open`'s own use of this extern.
            unsafe { deluge_streaming_resource_manager() }
        } else {
            self.manager
        };

        // A DIFFERENT Asset means this handle is being reused for a different sample, so nothing
        // about the current window survives: its leases pin the OLD sample's clusters, and its head
        // index is an index into the OLD sample's geometry. Rebind and rebuild from scratch.
        //
        // Without this, re-anchoring silently pinned the previous sample's clusters at the new
        // sample's marker -- reporting healthy coverage the whole time, because the pins it took
        // were real, just for the wrong sample. That is the sample-preview E199: rapid browser
        // scrolling reuses one SampleHolder across samples faster than its close path runs, so the
        // warm hint materialized the wrong Asset's cluster 0 and the note-on found its own cluster 0
        // cold. Dropping the old leases here is what makes a reused handle safe.
        let rebinding = asset != self.asset;
        if rebinding {
            self.leases = core::array::from_fn(|_| None); // release the old Asset's pins
            self.asset = asset;
            self.head_index = None; // the guard below must not compare across geometries
        }

        let geometry = resolve_geometry(manager, self.asset, marker_frame);
        let new_head = geometry.as_ref().map(|g| g.head);
        if new_head.is_some() && new_head == self.head_index {
            return; // Guard: still anchored on the same cluster -- no lease churn.
        }

        // Release every lease this reservation currently holds BEFORE acquiring any new one (see
        // this method's own doc for why the order matters).
        self.leases = core::array::from_fn(|_| None);
        self.manager = manager;

        match geometry {
            None => {
                self.head_index = None;
                self.covered = [0; DEPTH];
                self.num_covered = 0;
            }
            Some(geometry) => {
                let (covered, leases, num_covered) =
                    walk_and_lease(manager, self.asset, &geometry, direction, load_mode);
                self.head_index = Some(geometry.head);
                self.covered = covered;
                self.num_covered = num_covered;
                self.leases = leases;
            }
        }
    }

    /// The cluster indices this reservation covers, in walk order (test-visible; also
    /// [`Reservation::reanchor`]'s own bookkeeping surface).
    pub fn covered_indices(&self) -> &[u32] {
        &self.covered[..self.num_covered]
    }

    /// How many covered clusters this reservation actually holds a lease on.
    ///
    /// This is deliberately NOT the same as `covered_indices().len()`: `walk_and_lease` counts a
    /// cluster as covered as soon as it is in range, *before* asking `load_cluster` for a lease, and
    /// records `None` when that lease fails. So the two counts diverge exactly when a load failed —
    /// a `LoadMode::Now` whose synchronous fill returned false, or a reservation the manager could
    /// not satisfy — and comparing them is the only way to see that from outside. Every such
    /// failure is otherwise silent: `open` returns a perfectly valid handle either way, so a caller
    /// that asked for `DELUGE_LOAD_NOW` cannot currently tell whether anything was materialized.
    pub fn num_leased(&self) -> usize {
        self.leases.iter().filter(|lease| lease.is_some()).count()
    }
}

/// The resolved geometry needed to walk a reservation's covered window from its head cluster — the
/// shared first half of [`Reservation::open`]'s and [`Reservation::reanchor`]'s work (see either's
/// own doc for the full resolution recipe).
struct WalkGeometry {
    /// The cluster index the marker frame mapped to.
    head: u32,
    /// The cluster containing `audio_data_start_pos_bytes` itself — the walk's forward-clamp floor.
    first_with_data: u32,
    /// The registered `first_cluster_index_with_no_audio_data`, or `i64::MAX` (no bound) if that
    /// field is negative — the walk's clamp ceiling.
    first_no_data: i64,
    /// The registered `FillContext`'s own cluster size in bytes, threaded through to
    /// [`load_cluster`] unchanged.
    cluster_size: u32,
}

/// Resolves `asset`'s registered geometry against `marker_frame`, mirroring [`Reservation::open`]'s
/// own resolution recipe exactly (see that method's doc for the full rationale). Returns `None` for
/// every case `open` itself treats as inert: a null `manager`, no registered `FillContext`, or a
/// malformed one (zero frame stride / an out-of-range cluster-size shift).
fn resolve_geometry(manager: *mut c_void, asset: u32, marker_frame: u64) -> Option<WalkGeometry> {
    if manager.is_null() {
        return None;
    }
    let ctx = deluge_sample_fill::fill_context_for(asset)?; // No registered geometry.
    let frame_stride = ctx.byte_depth as u64 * ctx.num_channels as u64;
    if frame_stride == 0 || ctx.cluster_size_magnitude >= 64 {
        return None; // Malformed geometry -- mirrors `reader::locate`'s own guard.
    }

    let start_byte = (ctx.audio_data_start_pos_bytes as u64)
        .saturating_add(marker_frame.saturating_mul(frame_stride));
    let head = (start_byte >> ctx.cluster_size_magnitude).min(u32::MAX as u64) as u32;
    let first_with_data =
        ((ctx.audio_data_start_pos_bytes as u64) >> ctx.cluster_size_magnitude) as u32;
    let first_no_data: i64 = if ctx.first_cluster_index_with_no_audio_data < 0 {
        i64::MAX // No known upper bound (e.g. still recording) -- never clamps.
    } else {
        ctx.first_cluster_index_with_no_audio_data as i64
    };

    Some(WalkGeometry {
        head,
        first_with_data,
        first_no_data,
        cluster_size: ctx.cluster_size,
    })
}

/// Walks up to [`DEPTH`] cluster indices from `geometry.head` in `direction`, clamped to
/// `[geometry.first_with_data, geometry.first_no_data)`, taking one lease per covered cluster via
/// [`load_cluster`] — the shared second half of [`Reservation::open`]'s and
/// [`Reservation::reanchor`]'s work (see either's own doc for the full walk/clamp rationale).
fn walk_and_lease(
    manager: *mut c_void,
    asset: u32,
    geometry: &WalkGeometry,
    direction: i8,
    load_mode: LoadMode,
) -> ([u32; DEPTH], [Option<Lease>; DEPTH], usize) {
    // SAFETY: `manager` is non-null (a live `WalkGeometry` only resolves from a non-null one — see
    // `resolve_geometry`) and, per the boot-singleton contract `deluge_streaming_resource_manager`
    // documents, live for the process's remaining life.
    let resource = unsafe { Resource::from_handle(manager as *mut DelugeResource) };

    let mut covered = [0u32; DEPTH];
    let mut leases: [Option<Lease>; DEPTH] = core::array::from_fn(|_| None);
    let mut num_covered = 0usize;
    let mut idx = geometry.head as i64;
    for slot in 0..DEPTH {
        if idx < geometry.first_with_data as i64 || idx >= geometry.first_no_data {
            break;
        }
        let cluster_index = idx as u32;
        covered[slot] = cluster_index;
        num_covered = slot + 1;
        leases[slot] = load_cluster(
            &resource,
            asset,
            cluster_index,
            geometry.cluster_size,
            load_mode,
        );
        idx += direction as i64;
    }
    (covered, leases, num_covered)
}

/// Schedule `chunk` onto the async loader if it isn't already ready — the exact two-call recipe the
/// C++ `CLUSTER_ENQUEUE` path runs after its own
/// `deluge_resource_request` (`Resource::request`'s C++ twin): `deluge_resource_loader_enqueue(mgr,
/// cluster->resource_slot, priority_rating)` (here, `Resource::loader_enqueue`) then
/// `deluge_streaming_signal_fill()` to wake the loader task. `priority_rating` for this
/// passive-lookahead path is `0xFFFF_FFFF` — the
/// C++ recipe's own lowest-urgency prefetch priority. A no-op if `chunk` is already ready (mirrors
/// the C++ `if (!cluster->loaded)` guard): a hit doesn't need scheduling, and neither does a chunk
/// some other caller already filled between `request` and this call.
///
/// Shared by both [`load_cluster`] paths that keep a fresh (miss) reservation without running a
/// synchronous fill: [`LoadMode::Enqueue`] always, and [`LoadMode::NowOrEnqueue`]'s fill-failure
/// fallback.
fn enqueue_unfilled(resource: &Resource, chunk: deluge_resource::facade::Chunk) {
    if resource.is_ready(chunk) {
        return;
    }
    resource.loader_enqueue(resource.slot_of(chunk), 0xFFFF_FFFF);
    // SAFETY: a bare wake call, no arguments -- see the extern's own doc.
    unsafe { deluge_streaming_signal_fill() };
}

/// The per-cluster load recipe: a cache hit (`acquire_leased`) is already resident+ready and
/// returns immediately, for every `load_mode` alike — never re-filled. A miss `request`s a fresh
/// reservation (reserve + construct, no I/O); what happens to that reservation next depends on
/// `load_mode`:
/// - [`LoadMode::Enqueue`]: schedule the reservation onto the async loader
///   ([`enqueue_unfilled`]) and hand it back reserved, unfilled — never blocks.
/// - [`LoadMode::Now`]: run the synchronous fill (`fill_now`) then `Resource::mark_ready`. If the
///   fill fails, the reservation is dropped (`None`) rather than kept unfilled, releasing it.
/// - [`LoadMode::NowOrEnqueue`]: attempt the same synchronous fill; on failure, KEEP the
///   reservation and schedule it onto the async loader ([`enqueue_unfilled`]) instead of dropping
///   it (`Some`, unfilled) — mirrors `CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE`'s fall back to the
///   async path under memory pressure rather than giving up the reservation outright.
fn load_cluster(
    resource: &Resource,
    asset: u32,
    cluster_index: u32,
    cluster_size_bytes: u32,
    load_mode: LoadMode,
) -> Option<Lease> {
    if let Some(lease) = resource.acquire_leased(asset, cluster_index) {
        return Some(lease); // Already resident+ready -- no fill needed under any load_mode.
    }
    let lease = resource.request(asset, cluster_index, cluster_size_bytes as usize)?;
    match load_mode {
        LoadMode::Enqueue => {
            enqueue_unfilled(resource, lease.chunk());
            Some(lease)
        }
        LoadMode::Now | LoadMode::NowOrEnqueue => {
            let backing = lease.chunk().as_ptr() as *mut c_void;
            if fill_now(backing) {
                resource.mark_ready(lease.chunk());
                Some(lease)
            } else if load_mode == LoadMode::NowOrEnqueue {
                enqueue_unfilled(resource, lease.chunk()); // Keep it enqueued, unfilled -- do not drop the reservation.
                Some(lease)
            } else {
                None // `lease` drops here, releasing the failed reservation.
            }
        }
    }
}

/// Opaque per-reservation handle, mirroring `DelugeSampleReservation`. Every non-null pointer this
/// module hands out from `open()` is a `Box<Reservation>` turned into a raw pointer via
/// `Box::into_raw` — `close()` is the only legal way to reclaim it (`Box::from_raw`). Mirrors
/// `abi::DelugeSampleReader`'s own convention exactly.
#[repr(C)]
pub struct DelugeSampleReservation {
    _opaque: [u8; 0],
}

/// Open a passive lookahead reservation over `source_id`. See the header doc
/// (`deluge_sample_reserve_open`) and [`Reservation::open`] for the full contract.
///
/// Never returns null: heap allocation only aborts on this dependency graph (no `#[panic_handler]`
/// unwind path — see `abi::deluge_sample_reader_open`'s own doc for the identical argument).
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub extern "C" fn deluge_sample_reserve_open(
    source_id: u32,
    marker_frame: u64,
    direction: i8,
    load_mode: LoadMode,
) -> *mut DelugeSampleReservation {
    let reservation = Reservation::open(source_id, marker_frame, direction, load_mode);
    Box::into_raw(Box::new(reservation)) as *mut DelugeSampleReservation
}

/// Re-anchor `res` to the cluster containing `marker_frame`. See the header doc
/// (`deluge_sample_reserve_move`) and [`Reservation::reanchor`] for the full contract.
///
/// # Safety
/// `res` must be a live pointer previously returned by `deluge_sample_reserve_open` and not yet
/// passed to `deluge_sample_reserve_close`.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reserve_move(
    res: *mut DelugeSampleReservation,
    source_id: u32,
    marker_frame: u64,
    direction: i8,
    load_mode: LoadMode,
) {
    // SAFETY: `res` is a live, not-yet-closed pointer per this fn's own contract — the same cast
    // validity `deluge_sample_reserve_close` relies on (same address, `Reservation`'s layout
    // underneath).
    let reservation = unsafe { &mut *(res as *mut Reservation) };
    reservation.reanchor(source_id, marker_frame, direction, load_mode);
}

/// Release `res` and every lease it still holds. No-op on a null `res`.
///
/// # Safety
/// `res`, if non-null, must be a live pointer previously returned by `deluge_sample_reserve_open`
/// and not yet passed to this function.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reserve_close(res: *mut DelugeSampleReservation) {
    if res.is_null() {
        return;
    }
    // SAFETY: `res` was returned by `deluge_sample_reserve_open` as `Box::into_raw(Box::new(..))`
    // cast to `*mut DelugeSampleReservation` (same address, `Reservation`'s layout underneath);
    // non-null per the check above; live and not yet closed per this fn's own contract. Reclaiming
    // it as `Box<Reservation>` and dropping releases every held lease exactly once.
    let reservation = unsafe { Box::from_raw(res as *mut Reservation) };
    drop(reservation);
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::host_streaming_stubs::{
        set_active_manager, set_fail_at_byte_offset, set_force_read_failure, TEST_LOCK,
    };
    use deluge_resource::manager::BACKING_SLAB;
    use deluge_resource::value::COST_IO;
    use deluge_sample_fill::{deluge_streaming_set_fill_context, FillContext};
    extern crate std;
    use std::ops::Range;
    use std::vec::Vec;

    const CLUSTER_SIZE_BYTES: u32 = 32768;
    const CLUSTER_SIZE_MAGNITUDE: u32 = 15; // 2^15 == 32768

    /// Real per-chunk backing needs `RUST_CHUNK_PAYLOAD_OFFSET` (front guard) plus
    /// `CLUSTER_SIZE_BYTES` (payload) plus 7 more bytes of trailing slack `native_finish` always
    /// touches — the SAME sizing `reader.rs`'s own `window_tests::BACKING_SIZE` uses, for the
    /// identical reason: this module's `LoadMode::Now` test is this harness's first REAL
    /// `fill_now` call (every other test only ever reserves via `LoadMode::Enqueue`, which never
    /// runs a real fill) — a bare `CLUSTER_SIZE_BYTES`-sized heap allocation would let that fill
    /// overrun its backing. Derived from the real offset (not a hand-rounded literal) so this
    /// harness never silently drifts out of sync with `StreamedChunk`'s own layout.
    const BACKING_SIZE: usize =
        (deluge_sample_fill::chunk::RUST_CHUNK_PAYLOAD_OFFSET + CLUSTER_SIZE_BYTES as usize + 7)
            .next_multiple_of(16);

    /// Placement-construct a real `StreamedChunk` at `dest` — `Resource::request` (the miss
    /// branch of `load_cluster`) requires SOME construct callback attached to the asset (a
    /// construct-less asset refuses `request` — see `deluge_resource`'s own
    /// `request_constructs_without_loading_then_leases` test), and this module's `LoadMode::Now`
    /// test runs a REAL `fill_now` against the resulting chunk (`native_begin`/`native_finish`
    /// reach it through `deluge_sample_fill::chunk`'s real, in-crate accessors), which requires a
    /// genuinely constructed backing to reborrow soundly — mirrors `reader.rs`'s own
    /// `window_tests::real_chunk_construct` (see `lib.rs`'s `host_streaming_stubs` module doc for
    /// why this crate no longer stubs the C-ABI construct/accessors themselves).
    ///
    /// # Safety
    /// `dest` must be a writable slab slot of at least `RUST_CHUNK_PAYLOAD_OFFSET +
    /// CLUSTER_SIZE_BYTES + 7` bytes — this module's own `BACKING_SIZE` slab satisfies that.
    unsafe extern "C" fn real_chunk_construct(
        ctx: *mut c_void,
        owner: *mut c_void,
        index: u32,
        dest: *mut u8,
    ) {
        // SAFETY: forwarding the caller's contract (above) to the real construct.
        unsafe {
            deluge_sample_fill::chunk::deluge_streaming_chunk_construct(
                ctx,
                owner,
                index,
                dest as *mut c_void,
            );
        }
    }

    /// Backing arena for the leaked test heap, kept alive for the process's remaining life —
    /// mirrors the sibling `reader.rs`/`abi.rs` test harnesses' own boot-singleton contract.
    struct TestHeap {
        _buf: Vec<u128>,
    }

    /// This module's own small test harness (a fresh copy per test module, not a shared type — see
    /// `abi.rs`'s doc for why this crate keeps one per file rather than sharing one): a manager plus
    /// one requestable asset whose registered [`FillContext`] places its real audio data across
    /// `clusters` (e.g. `2..6`, i.e. clusters 2, 3, 4, 5 have data; cluster 6 is the first with
    /// none), at `frame_stride` bytes/frame.
    struct TestHarness {
        asset: u32,
        handle: *mut DelugeResource,
        frame_stride: u32,
        first_with_data: u32,
    }

    impl TestHarness {
        fn with_audio_data_clusters(clusters: Range<u32>, frame_stride: u32) -> Self {
            let words = (2 * 1024 * 1024usize).div_ceil(16);
            let mut buf: Vec<u128> = std::vec![0u128; words];
            let ptr = buf.as_mut_ptr() as *mut u8;
            // SAFETY: `buf` is leaked below so the arena stays alive for the whole test binary's
            // life, matching the sibling crates' own boot-singleton test-harness contract.
            let h = unsafe { deluge_alloc::deluge_heap_create(ptr, words * 16) };
            std::mem::forget(TestHeap { _buf: buf });
            // SAFETY: `h` is the live heap handle just created above; `BACKING_SIZE`-byte slots
            // (real per-chunk backing, not just the raw cluster payload — see that const's own
            // doc) mirror production's own uniform streaming-cluster slab
            // (`chunk_residency.cpp`'s `deluge_streaming_define_asset`), the same convention
            // `reader.rs`'s own `window_tests` harness uses.
            let slab =
                unsafe { deluge_alloc::slab::deluge_slab_create_unmanaged(h, BACKING_SIZE, 16) };
            assert!(!slab.is_null());
            // SAFETY: `h` is the live heap handle just created above.
            let handle = unsafe { deluge_resource::deluge_resource_create(h, 4, 16) };
            assert!(!handle.is_null());
            // SAFETY: `handle`/`slab` are both live, over the same heap.
            unsafe { deluge_resource::deluge_resource_set_slab(handle, slab) };
            // SAFETY: `handle` is live; `real_chunk_construct` has the required C-ABI signature.
            let asset = unsafe {
                deluge_resource::deluge_resource_define_asset(
                    handle,
                    core::ptr::null_mut(),
                    None,
                    None,
                    core::ptr::null_mut(),
                    COST_IO,
                    BACKING_SLAB,
                )
            };
            unsafe {
                deluge_resource::deluge_resource_set_construct(
                    handle,
                    asset,
                    Some(real_chunk_construct),
                );
            }
            let ctx = FillContext {
                efatfs_handle: 0,
                audio_data_start_pos_bytes: clusters.start * CLUSTER_SIZE_BYTES,
                audio_data_length_bytes: (clusters.end - clusters.start) as u64
                    * CLUSTER_SIZE_BYTES as u64,
                first_cluster_index_with_no_audio_data: clusters.end as i32,
                cluster_size: CLUSTER_SIZE_BYTES,
                cluster_size_magnitude: CLUSTER_SIZE_MAGNITUDE,
                raw_data_format: 0,
                byte_depth: 2,
                num_channels: (frame_stride / 2) as u8,
            };
            deluge_streaming_set_fill_context(core::ptr::null_mut(), asset, ctx);
            set_active_manager(handle as *mut c_void);
            TestHarness {
                asset,
                handle,
                frame_stride,
                first_with_data: clusters.start,
            }
        }

        /// A marker frame that lands exactly at the FIRST byte of cluster `c` — i.e.
        /// `audio_data_start_pos_bytes + frame * frame_stride == c * CLUSTER_SIZE_BYTES`.
        fn frame_in_cluster(&self, c: u32) -> u64 {
            let target_byte = c as u64 * CLUSTER_SIZE_BYTES as u64;
            let start_byte = self.first_with_data as u64 * CLUSTER_SIZE_BYTES as u64;
            (target_byte - start_byte) / self.frame_stride as u64
        }

        fn resource(&self) -> Resource<'_> {
            // SAFETY: `self.handle` is live for as long as `self` (and its `_buf`, via the leaked
            // `TestHeap`).
            unsafe { Resource::from_handle(self.handle) }
        }

        /// Sum of `lease_count_by_slot(slot_of(chunk))` over every cluster index in `indices` that
        /// is currently resident (`Resource::peek`, itself lease-free and recency-neutral) — a
        /// cluster never requested/leased contributes 0, matching a plain sum over "how many leases
        /// this reservation is still holding across these clusters".
        fn total_leases_over(&self, indices: &[u32]) -> u32 {
            let resource = self.resource();
            indices
                .iter()
                .filter_map(|&index| resource.peek(self.asset, index))
                .map(|chunk| resource.lease_count_by_slot(resource.slot_of(chunk)))
                .sum()
        }

        /// A monotonic counter that bumps on every `load_cluster` call this reservation's
        /// `LoadMode::Enqueue` recipe actually runs (hit or miss alike) — `deluge_resource`'s own
        /// `Stats::requests`, read via the crate's public `deluge_resource_stats` FFI (no
        /// manager-internal access needed).
        ///
        /// This works as a lease-churn detector specifically BECAUSE, under `LoadMode::Enqueue`
        /// (every caller of this helper), `real_chunk_construct`/the recipe itself never calls
        /// `mark_ready`: every chunk stays permanently un-ready, so `load_cluster`'s first attempt
        /// (`Resource::acquire_leased`, which only hits a `ready` chunk) NEVER hits — it always
        /// falls through to `Resource::request`, which bumps `Stats::requests` unconditionally, on
        /// both its own cache-hit and fresh-alloc paths (see `Manager::request`). So this delta is
        /// a faithful count of "how many times a covered cluster's load recipe ran" over an
        /// interval, independent of residency/lease-count state that (unlike this counter) returns
        /// to its original value across a spurious release-then-reacquire cycle and so cannot, on
        /// its own, prove one never happened. (`LoadMode::Now`/`NowOrEnqueue`, which DO call
        /// `mark_ready` on a successful fill, would break this invariant — no caller of this helper
        /// uses either.)
        fn acquire_generation(&self) -> u64 {
            let mut stats = deluge_resource::Stats::default();
            // SAFETY: `self.handle` is live for as long as `self`.
            unsafe { deluge_resource::deluge_resource_stats(self.handle, &mut stats) };
            stats.requests
        }

        /// Is cluster `index` of this harness's asset currently resident AND ready
        /// (`Resource::peek` + `Resource::is_ready`, both lease-free) — the load-mode test's own
        /// readiness query: `false` for a never-touched index, an `Enqueue`d-but-unfilled
        /// reservation (`request` leaves `ready = false` until something calls `mark_ready`), or a
        /// no-longer-resident one; `true` only once a real fill has published it.
        fn is_ready_at(&self, index: u32) -> bool {
            let resource = self.resource();
            resource
                .peek(self.asset, index)
                .is_some_and(|chunk| resource.is_ready(chunk))
        }

        /// Force-evict every currently resident chunk of this harness's asset — test-setup
        /// hygiene for a test that needs to start from "nothing resident", made explicit rather
        /// than relying on this being a freshly-constructed manager (`deluge_resource_evict_chunk`
        /// no-ops on a non-resident/still-leased pointer, so this is safe to call with nothing
        /// leased, which is every caller's own precondition). Sweeps a fixed range wide enough to
        /// cover every cluster index this module's tests ever touch.
        fn evict_all(&self) {
            let resource = self.resource();
            for index in 0..32 {
                if let Some(chunk) = resource.peek(self.asset, index) {
                    // SAFETY: `self.handle` is live for as long as `self`; `chunk.as_ptr()` is a
                    // pointer `peek` just reported resident under this same handle.
                    unsafe {
                        deluge_resource::deluge_resource_evict_chunk(self.handle, chunk.as_ptr())
                    };
                }
            }
        }
    }

    #[test]
    fn open_forward_pins_two_units_from_the_marker_cluster() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        // marker frame lands in cluster 3
        let res = Reservation::open(h.asset, h.frame_in_cluster(3), 1, LoadMode::Enqueue);
        assert_eq!(res.covered_indices(), &[3, 4]);
    }

    #[test]
    fn open_reverse_pins_two_units_descending() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        let res = Reservation::open(h.asset, h.frame_in_cluster(4), -1, LoadMode::Enqueue);
        assert_eq!(res.covered_indices(), &[4, 3]);
    }

    #[test]
    fn open_clamps_forward_at_first_cluster_with_no_audio_data() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        let res = Reservation::open(h.asset, h.frame_in_cluster(5), 1, LoadMode::Enqueue);
        assert_eq!(res.covered_indices(), &[5]); // 6 is out of range, dropped
    }

    #[test]
    fn open_clamps_reverse_at_first_cluster_with_audio_data() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        let res = Reservation::open(h.asset, h.frame_in_cluster(2), -1, LoadMode::Enqueue);
        assert_eq!(res.covered_indices(), &[2]); // 1 is out of range, dropped
    }

    #[test]
    fn open_takes_a_lease_per_covered_unit_and_close_releases_all() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        let res = Reservation::open(h.asset, h.frame_in_cluster(3), 1, LoadMode::Enqueue);
        assert_eq!(h.total_leases_over(&[3, 4]), 2);
        drop(res);
        assert_eq!(h.total_leases_over(&[3, 4]), 0);
    }

    /// A null manager (no `set_active_manager` call on this thread) produces a fully inert
    /// reservation — no leases, empty coverage — the same shape `peek` returns null for.
    #[test]
    fn open_with_no_active_manager_is_inert() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        set_active_manager(core::ptr::null_mut());
        let res = Reservation::open(0, 0, 1, LoadMode::Enqueue);
        assert!(res.covered_indices().is_empty());
    }

    /// An asset with no registered `FillContext` (e.g. never streamed) also produces an inert
    /// reservation, even with a perfectly live manager.
    #[test]
    fn open_over_an_unregistered_asset_is_inert() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        let never_registered_asset = h.asset + 1000;
        let res = Reservation::open(never_registered_asset, 0, 1, LoadMode::Enqueue);
        assert!(res.covered_indices().is_empty());
    }

    /// The guard: a marker that drifts but stays inside the SAME head cluster must not release or
    /// re-acquire a single lease — see `acquire_generation`'s own doc for why its delta (rather than
    /// a lease-count snapshot, which returns to its original value across a spurious
    /// release-then-reacquire cycle) is what actually proves this.
    #[test]
    fn move_within_the_same_head_unit_is_a_no_op() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        let mut res = Reservation::open(h.asset, h.frame_in_cluster(3), 1, LoadMode::Enqueue);
        assert_eq!(res.covered_indices(), &[3, 4]);

        let before = h.acquire_generation();
        res.reanchor(h.asset, h.frame_in_cluster(3) + 10, 1, LoadMode::Enqueue); // still cluster 3
        assert_eq!(res.covered_indices(), &[3, 4]);
        assert_eq!(
            h.acquire_generation(),
            before,
            "no lease churn when head unit unchanged"
        );
    }

    /// Crossing a cluster boundary releases the old head's lease and pins the new window —
    /// release-old-then-acquire-new, exercised end to end via real lease-count observation.
    #[test]
    fn move_across_a_unit_boundary_rebuilds_the_window() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        let mut res = Reservation::open(h.asset, h.frame_in_cluster(3), 1, LoadMode::Enqueue);
        assert_eq!(res.covered_indices(), &[3, 4]);

        res.reanchor(h.asset, h.frame_in_cluster(4), 1, LoadMode::Enqueue);
        assert_eq!(res.covered_indices(), &[4, 5]);
        assert_eq!(h.total_leases_over(&[3]), 0, "old head released");
        assert_eq!(h.total_leases_over(&[4, 5]), 2, "new window pinned");
    }

    /// Re-anchoring onto a DIFFERENT Asset rebinds the reservation and drops every pin it held on
    /// the old one.
    ///
    /// This is the sample-preview E199, proven on hardware 2026-08-16: a `SampleHolder` reused for
    /// a different sample took `claimClusterReasonsForMarker`'s move branch, and `reanchor` — which
    /// used to keep its opening Asset forever — re-anchored the OLD sample's window onto the NEW
    /// sample's marker. It reported healthy coverage throughout, because the pins it took were
    /// real; they were simply for the wrong sample, leaving the note-on's own cluster 0 cold.
    #[test]
    fn move_to_a_different_asset_rebinds_and_releases_the_old_pins() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        let mut res = Reservation::open(h.asset, h.frame_in_cluster(3), 1, LoadMode::Enqueue);
        assert_eq!(res.covered_indices(), &[3, 4]);
        assert_eq!(h.total_leases_over(&[3, 4]), 2, "opening window pinned");

        // A different Asset id. It has no registered geometry, so the rebuilt window is inert —
        // which is exactly right: the reservation must NOT keep reporting the old Asset's clusters.
        let other_asset = h.asset + 1;
        res.reanchor(other_asset, h.frame_in_cluster(3), 1, LoadMode::Enqueue);

        assert_eq!(res.asset, other_asset, "rebound to the new Asset");
        assert_eq!(
            h.total_leases_over(&[3, 4]),
            0,
            "every pin on the OLD Asset released -- the bug left these held for the wrong sample"
        );
        assert!(
            res.covered_indices().is_empty(),
            "no stale coverage carried across the rebind"
        );
    }

    /// `LoadMode::Now` runs a real synchronous fill (see [`load_cluster`]'s own doc) where
    /// `LoadMode::Enqueue` only reserves. Real
    /// end-to-end proof, not vacuous: `is_ready_at` reads the manager's own readiness flag,
    /// flipped only by `Resource::mark_ready` after a genuine `fill_now` call runs the real
    /// `deluge_sample_fill::{native_begin, native_finish}` pair against this crate's own
    /// `deluge_efatfs_read_at` host stub — see `BACKING_SIZE`'s own doc for why this harness's
    /// asset is slab-backed with real per-chunk sizing (rather than the bare-`CLUSTER_SIZE_BYTES`
    /// heap sizing every other, never-filling test here could get away with): a real fill through
    /// an undersized backing would be an out-of-bounds write, not just a wrong assertion.
    #[test]
    fn load_now_materializes_where_enqueue_does_not() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        set_force_read_failure(false);
        set_fail_at_byte_offset(None);
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        h.evict_all(); // ensure nothing resident

        let enq = Reservation::open(h.asset, h.frame_in_cluster(3), 1, LoadMode::Enqueue);
        assert!(!h.is_ready_at(3), "enqueue does not fill");
        drop(enq);

        let now = Reservation::open(h.asset, h.frame_in_cluster(3), 1, LoadMode::Now);
        assert!(h.is_ready_at(3), "load-now fills synchronously");
        drop(now);
    }

    /// The bug this test would have caught: `LoadMode::Enqueue` must actually SCHEDULE its
    /// covered, freshly-reserved (miss) clusters onto the real loader queue
    /// (`Resource::loader_next`), not just take a lease and leave them stranded. Before the fix,
    /// `load_cluster`'s `Enqueue` arm handed back `resource.request(...)`'s lease without ever
    /// calling `Resource::loader_enqueue` — `loader_next()` would return `None` here. Exercised
    /// through the real `deluge_resource` loader queue (`h.resource()`), not a mock.
    #[test]
    fn open_enqueue_schedules_covered_clusters_on_the_loader() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        h.evict_all(); // ensure nothing resident -- every covered cluster is a fresh miss

        let res = Reservation::open(h.asset, h.frame_in_cluster(3), 1, LoadMode::Enqueue);
        assert_eq!(res.covered_indices(), &[3, 4]);

        let resource = h.resource();
        let mut scheduled: Vec<(u32, u32)> = Vec::new();
        while let Some(chunk) = resource.loader_next() {
            scheduled.push(
                resource
                    .chunk_ident(chunk)
                    .expect("popped chunk is resident"),
            );
        }
        scheduled.sort_unstable();
        assert_eq!(
            scheduled,
            std::vec![(h.asset, 3), (h.asset, 4)],
            "both covered (miss) clusters must have been enqueued on the real loader queue"
        );
        assert!(
            resource.loader_next().is_none(),
            "the loader queue must be empty once every scheduled chunk has been popped"
        );

        drop(res);
    }
}
