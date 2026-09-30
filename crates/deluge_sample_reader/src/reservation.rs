//! The lookahead reservation handle (`DelugeSampleReservation` in `libdeluge/sample_reader.h`).
//!
//! Where [`crate::reader::peek`] glances at what is already resident, a reservation pins a small
//! forward or backward window of clusters (one `Lease` each) for as long as it stays open — the
//! `kNumClustersLoadedAhead` lookahead the timestretch and loop-point paths use.
//! [`Reservation::reanchor`] slides the window as playback advances, without lease churn while the
//! marker stays in its head cluster. See `load_cluster` for what each [`LoadMode`] does.

use alloc::boxed::Box;
use core::ffi::c_void;

use deluge_resource::facade::{Lease, Resource};
use deluge_resource::DelugeResource;

/// Clusters pinned from the marker cluster onward, in the play direction. Mirrors
/// `kNumClustersLoadedAhead` (`src/definitions_cxx.hpp`).
const DEPTH: usize = 2;

/// Mirrors `DelugeLoadMode` (`libdeluge/sample_reader.h`). `repr(u8)` matches the header's
/// `: uint8_t`, as it crosses the FFI by value.
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum LoadMode {
    /// Reserve the covered clusters and enqueue them for the async loader; never blocks.
    Enqueue = 0,
    /// Materialize the covered clusters synchronously before returning. A cluster whose fill fails
    /// is released.
    Now = 1,
    /// Load synchronously; if the fill fails, keep the reservation and enqueue it instead. Mirrors
    /// `CLUSTER_LOAD_IMMEDIATELY_OR_ENQUEUE`.
    NowOrEnqueue = 2,
}

unsafe extern "C" {
    /// Returns the process-wide resource manager (`libdeluge/sample_stream.h`).
    pub fn deluge_streaming_resource_manager() -> *mut c_void;
    /// Reads synchronously from the card (`libdeluge/file_io.h`); see `fill_now`.
    pub fn deluge_efatfs_read_at(
        handle: deluge_sample_fill::DelugeHandle,
        byte_offset: u32,
        dst: *mut c_void,
        count: u32,
        out_read: *mut u32,
    ) -> deluge_sample_fill::DelugeStatus;
    /// Wakes the async loader after a `Resource::loader_enqueue` (`libdeluge/streaming_fill.h`).
    pub fn deluge_streaming_signal_fill();
}

/// Fills `chunk_backing` synchronously: `native_begin` resolves the span, one
/// `deluge_efatfs_read_at` reads it, and `native_finish` converts, stitches and publishes.
///
/// The BSP's async fill task composes the same two primitives around an awaited read. Returns
/// `false` on a geometry failure, a failed or short read, or a failed convert/stitch/publish.
pub(crate) fn fill_now(chunk_backing: *mut c_void) -> bool {
    let d = deluge_sample_fill::native_begin(chunk_backing);
    if !d.ok {
        return false;
    }
    let count = d.num_sectors * 512;
    let mut out_read: u32 = 0;
    // SAFETY: `d.dest` is this chunk's payload buffer, which `native_begin` sized from the same
    // geometry and is valid for `count` bytes; `out_read` is a local.
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

/// A lookahead reservation over one sample's residency: the state behind `DelugeSampleReservation`.
pub struct Reservation {
    /// The asset (the header's `source_id`) this reservation covers.
    asset: u32,
    /// The resource manager, or null if none existed at `open`. Kept even when inert so
    /// [`Reservation::reanchor`] can reuse it.
    manager: *mut c_void,
    /// The cluster the marker mapped to at the last `open`/`reanchor`, or `None` if inert (no
    /// manager or no valid geometry). A live reservation can still have empty coverage, if its head
    /// cluster lies outside the audio data.
    head_index: Option<u32>,
    /// Covered cluster indices in walk order (marker outward), clamped to the audio-data range.
    /// `covered[i]` is the cluster `leases[i]` was requested for.
    covered: [u32; DEPTH],
    /// The number of valid entries in `covered`.
    num_covered: usize,
    /// One lease per covered cluster; `None` where the load failed (OOM, nothing evictable, a
    /// construct-less asset, or a failed `Now` fill). A failed load does not shrink the coverage.
    /// Dropping the reservation releases every lease exactly once.
    leases: [Option<Lease>; DEPTH],
}

impl Reservation {
    /// Opens a reservation over `asset`, pinning up to `DEPTH` clusters from the one
    /// `marker_frame` maps to, walking in `direction` (+1 forward, -1 reverse).
    ///
    /// A null manager, an unregistered asset, or malformed geometry gives an inert reservation (no
    /// leases, `head_index = None`), the cases where `peek` returns null.
    ///
    /// The head cluster is `(audio_data_start_pos_bytes + marker_frame * frame_stride) >>
    /// cluster_size_magnitude`. The walk stops at the first index outside `[first_with_data,
    /// first_no_data)`: the cluster holding `audio_data_start_pos_bytes`, and
    /// `first_cluster_index_with_no_audio_data` (unbounded when negative, as while recording).
    ///
    /// See `load_cluster` for the per-cluster recipe. `LoadMode::Now` and
    /// `LoadMode::NowOrEnqueue` block on the synchronous fill of each cluster not already ready;
    /// `LoadMode::Enqueue` never blocks.
    pub fn open(asset: u32, marker_frame: u64, direction: i8, load_mode: LoadMode) -> Reservation {
        // SAFETY: no preconditions; returns the process-wide manager or null.
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

    /// Re-anchors to the cluster containing `marker_frame`, walking as [`Reservation::open`] does
    /// (`deluge_sample_reserve_move`).
    ///
    /// If the new head cluster equals the current one, this returns without touching a lease, so a
    /// marker drifting within its cluster each render tick causes no churn. Otherwise every held
    /// lease is released before any new one is taken, so a stale pin never keeps an evictable slot
    /// warm. A resolution failure leaves the inert shape `open` returns.
    pub fn reanchor(&mut self, asset: u32, marker_frame: u64, direction: i8, load_mode: LoadMode) {
        // Retry the lookup if there was no manager at `open`: streaming boot may have finished.
        let manager = if self.manager.is_null() {
            // SAFETY: no preconditions.
            unsafe { deluge_streaming_resource_manager() }
        } else {
            self.manager
        };

        // A different asset means the handle is being reused for another sample (browser scrolling
        // reuses one `SampleHolder`): the old leases and head index belong to the old sample, so
        // rebind and rebuild. Otherwise the old sample's clusters stay pinned while the new one's
        // cluster 0 is cold, with healthy-looking coverage (error E199 on preview).
        let rebinding = asset != self.asset;
        if rebinding {
            self.leases = core::array::from_fn(|_| None); // Release the old asset's pins.
            self.asset = asset;
            self.head_index = None; // The guard below must not compare across geometries.
        }

        let geometry = resolve_geometry(manager, self.asset, marker_frame);
        let new_head = geometry.as_ref().map(|g| g.head);
        if new_head.is_some() && new_head == self.head_index {
            return; // Same head cluster: no lease churn.
        }

        // Release before acquiring (see the doc above).
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

    /// Returns the covered cluster indices, in walk order.
    pub fn covered_indices(&self) -> &[u32] {
        &self.covered[..self.num_covered]
    }
}

/// The resolved geometry a reservation's walk needs.
struct WalkGeometry {
    /// The cluster index the marker frame mapped to.
    head: u32,
    /// The cluster containing `audio_data_start_pos_bytes`: the walk's lower bound.
    first_with_data: u32,
    /// `first_cluster_index_with_no_audio_data`, or `i64::MAX` when that is negative: the walk's
    /// exclusive upper bound.
    first_no_data: i64,
    /// Cluster size in bytes.
    cluster_size: u32,
}

/// Resolves `asset`'s registered geometry against `marker_frame`. Returns `None` for a null
/// `manager`, no registered `FillContext`, or a malformed one (zero frame stride or a shift of 64
/// or more).
fn resolve_geometry(manager: *mut c_void, asset: u32, marker_frame: u64) -> Option<WalkGeometry> {
    if manager.is_null() {
        return None;
    }
    let ctx = deluge_sample_fill::fill_context_for(asset)?; // No registered geometry.
    let frame_stride = ctx.byte_depth as u64 * ctx.num_channels as u64;
    if frame_stride == 0 || ctx.cluster_size_magnitude >= 64 {
        return None; // Malformed geometry; same guard as `reader::locate`.
    }

    let start_byte = (ctx.audio_data_start_pos_bytes as u64)
        .saturating_add(marker_frame.saturating_mul(frame_stride));
    let head = (start_byte >> ctx.cluster_size_magnitude).min(u32::MAX as u64) as u32;
    let first_with_data =
        ((ctx.audio_data_start_pos_bytes as u64) >> ctx.cluster_size_magnitude) as u32;
    let first_no_data: i64 = if ctx.first_cluster_index_with_no_audio_data < 0 {
        i64::MAX // No known end (still recording).
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

/// Walks up to [`DEPTH`] clusters from `geometry.head` in `direction`, stopping outside
/// `[first_with_data, first_no_data)`, and takes one lease per covered cluster via
/// [`load_cluster`].
fn walk_and_lease(
    manager: *mut c_void,
    asset: u32,
    geometry: &WalkGeometry,
    direction: i8,
    load_mode: LoadMode,
) -> ([u32; DEPTH], [Option<Lease>; DEPTH], usize) {
    // SAFETY: `manager` is non-null (`resolve_geometry` checked it) and, as the process-wide
    // manager, lives for the rest of the process.
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

/// Enqueues `chunk` on the async loader at lowest priority (`0xFFFF_FFFF`) and wakes it, unless the
/// chunk is already ready. The same two calls as the C++ `CLUSTER_ENQUEUE` path.
fn enqueue_unfilled(resource: &Resource, chunk: deluge_resource::facade::Chunk) {
    if resource.is_ready(chunk) {
        return;
    }
    resource.loader_enqueue(resource.slot_of(chunk), 0xFFFF_FFFF);
    // SAFETY: no preconditions.
    unsafe { deluge_streaming_signal_fill() };
}

/// Loads one cluster and returns its lease, or `None` on failure.
///
/// A ready cluster (`acquire_leased`) is returned as is for every mode. A miss `request`s a fresh
/// reservation (no I/O), then:
/// - [`LoadMode::Enqueue`]: enqueues it ([`enqueue_unfilled`]) and returns it unfilled; never
///   blocks.
/// - [`LoadMode::Now`]: fills it ([`fill_now`]) and marks it ready; on failure releases it.
/// - [`LoadMode::NowOrEnqueue`]: fills it; on failure keeps it and enqueues it instead.
fn load_cluster(
    resource: &Resource,
    asset: u32,
    cluster_index: u32,
    cluster_size_bytes: u32,
    load_mode: LoadMode,
) -> Option<Lease> {
    if let Some(lease) = resource.acquire_leased(asset, cluster_index) {
        return Some(lease); // Already ready.
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
                enqueue_unfilled(resource, lease.chunk()); // Keep the reservation, unfilled.
                Some(lease)
            } else {
                None // Dropping `lease` releases the failed reservation.
            }
        }
    }
}

/// Opaque reservation handle, mirroring `DelugeSampleReservation`.
///
/// Every pointer `deluge_sample_reserve_open` returns is a `Box<Reservation>` from `Box::into_raw`;
/// only `deluge_sample_reserve_close` may reclaim it.
#[repr(C)]
pub struct DelugeSampleReservation {
    _opaque: [u8; 0],
}

/// Opens a lookahead reservation over `source_id`. See [`Reservation::open`].
///
/// Never returns null: allocation failure aborts rather than returning.
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

/// Re-anchors `res` to the cluster containing `marker_frame`. See [`Reservation::reanchor`].
///
/// # Safety
///
/// `res` must be a live pointer from `deluge_sample_reserve_open` not yet passed to
/// `deluge_sample_reserve_close`.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reserve_move(
    res: *mut DelugeSampleReservation,
    source_id: u32,
    marker_frame: u64,
    direction: i8,
    load_mode: LoadMode,
) {
    // SAFETY: a live `Box<Reservation>` pointer per this function's contract.
    let reservation = unsafe { &mut *(res as *mut Reservation) };
    reservation.reanchor(source_id, marker_frame, direction, load_mode);
}

/// Releases `res` and every lease it holds. A no-op on a null `res`.
///
/// # Safety
///
/// `res` must be null or a live pointer from `deluge_sample_reserve_open` not yet passed to this
/// function.
#[cfg_attr(any(target_os = "none", feature = "c_abi"), unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_reserve_close(res: *mut DelugeSampleReservation) {
    if res.is_null() {
        return;
    }
    // SAFETY: non-null, and the live `Box<Reservation>` pointer `open` produced, per this
    // function's contract. Dropping it releases every lease exactly once.
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

    /// Real per-chunk backing: the `StreamedChunk` header, the payload, and the 7 bytes of trailing
    /// slack `native_finish` touches. The `LoadMode::Now` test runs a real fill, which would
    /// overrun a bare `CLUSTER_SIZE_BYTES` allocation.
    const BACKING_SIZE: usize =
        (deluge_sample_fill::chunk::RUST_CHUNK_PAYLOAD_OFFSET + CLUSTER_SIZE_BYTES as usize + 7)
            .next_multiple_of(16);

    /// Placement-constructs a real `StreamedChunk` at `dest`.
    ///
    /// `Resource::request` refuses an asset with no construct callback, and the `LoadMode::Now`
    /// test fills the chunk for real, which needs a genuinely constructed backing.
    ///
    /// # Safety
    ///
    /// `dest` must be writable for [`BACKING_SIZE`] bytes.
    unsafe extern "C" fn real_chunk_construct(
        ctx: *mut c_void,
        owner: *mut c_void,
        index: u32,
        dest: *mut u8,
    ) {
        // SAFETY: the caller's contract is the real construct's.
        unsafe {
            deluge_sample_fill::chunk::deluge_streaming_chunk_construct(
                ctx,
                owner,
                index,
                dest as *mut c_void,
            );
        }
    }

    /// Backing arena for the leaked test heap.
    struct TestHeap {
        _buf: Vec<u128>,
    }

    /// A manager plus one requestable asset whose audio data spans `clusters` (for `2..6`, clusters
    /// 2-5 hold data and 6 is the first without), at `frame_stride` bytes per frame.
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
            // SAFETY: `buf` is leaked below, so the arena outlives the test.
            let h = unsafe { deluge_alloc::deluge_heap_create(ptr, words * 16) };
            std::mem::forget(TestHeap { _buf: buf });
            // SAFETY: `h` is live. Uniform `BACKING_SIZE` slots, like production's streaming slab.
            let slab =
                unsafe { deluge_alloc::slab::deluge_slab_create_unmanaged(h, BACKING_SIZE, 16) };
            assert!(!slab.is_null());
            // SAFETY: `h` is live.
            let handle = unsafe { deluge_resource::deluge_resource_create(h, 4, 16) };
            assert!(!handle.is_null());
            // SAFETY: `handle` and `slab` are live, over the same heap.
            unsafe { deluge_resource::deluge_resource_set_slab(handle, slab) };
            // SAFETY: `handle` is live.
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

        /// Returns the marker frame at the first byte of cluster `c`.
        fn frame_in_cluster(&self, c: u32) -> u64 {
            let target_byte = c as u64 * CLUSTER_SIZE_BYTES as u64;
            let start_byte = self.first_with_data as u64 * CLUSTER_SIZE_BYTES as u64;
            (target_byte - start_byte) / self.frame_stride as u64
        }

        fn resource(&self) -> Resource<'_> {
            // SAFETY: `self.handle` lives as long as `self`.
            unsafe { Resource::from_handle(self.handle) }
        }

        /// Returns the total lease count over the resident clusters in `indices`.
        fn total_leases_over(&self, indices: &[u32]) -> u32 {
            let resource = self.resource();
            indices
                .iter()
                .filter_map(|&index| resource.peek(self.asset, index))
                .map(|chunk| resource.lease_count_by_slot(resource.slot_of(chunk)))
                .sum()
        }

        /// Returns the manager's `Stats::requests`, a lease-churn detector.
        ///
        /// Under `LoadMode::Enqueue` (every caller) nothing is marked ready, so `acquire_leased`
        /// never hits and every `load_cluster` call reaches `request`, which always bumps the
        /// counter. Unlike a lease count, it does not return to its old value across a spurious
        /// release-and-reacquire.
        fn acquire_generation(&self) -> u64 {
            let mut stats = deluge_resource::Stats::default();
            // SAFETY: `self.handle` lives as long as `self`.
            unsafe { deluge_resource::deluge_resource_stats(self.handle, &mut stats) };
            stats.requests
        }

        /// Returns whether cluster `index` is resident and ready (only after a real fill).
        fn is_ready_at(&self, index: u32) -> bool {
            let resource = self.resource();
            resource
                .peek(self.asset, index)
                .is_some_and(|chunk| resource.is_ready(chunk))
        }

        /// Evicts every resident chunk of the asset (indices 0..32). Callers hold no leases.
        fn evict_all(&self) {
            let resource = self.resource();
            for index in 0..32 {
                if let Some(chunk) = resource.peek(self.asset, index) {
                    // SAFETY: `self.handle` is live; `peek` just reported `chunk` resident.
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

    #[test]
    fn open_with_no_active_manager_is_inert() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        set_active_manager(core::ptr::null_mut());
        let res = Reservation::open(0, 0, 1, LoadMode::Enqueue);
        assert!(res.covered_indices().is_empty());
    }

    #[test]
    fn open_over_an_unregistered_asset_is_inert() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        let never_registered_asset = h.asset + 1000;
        let res = Reservation::open(never_registered_asset, 0, 1, LoadMode::Enqueue);
        assert!(res.covered_indices().is_empty());
    }

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

    /// Guards error E199 on preview: a `SampleHolder` reused for another sample must not keep
    /// pinning the old sample's clusters.
    #[test]
    fn move_to_a_different_asset_rebinds_and_releases_the_old_pins() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        let mut res = Reservation::open(h.asset, h.frame_in_cluster(3), 1, LoadMode::Enqueue);
        assert_eq!(res.covered_indices(), &[3, 4]);
        assert_eq!(h.total_leases_over(&[3, 4]), 2, "opening window pinned");

        // The other asset has no geometry, so the rebuilt window is inert.
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

    /// `is_ready_at` flips only after a real `fill_now` through the host read stub, which is why
    /// this harness sizes its slab to [`BACKING_SIZE`].
    #[test]
    fn load_now_materializes_where_enqueue_does_not() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        set_force_read_failure(false);
        set_fail_at_byte_offset(None);
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        h.evict_all();

        let enq = Reservation::open(h.asset, h.frame_in_cluster(3), 1, LoadMode::Enqueue);
        assert!(!h.is_ready_at(3), "enqueue does not fill");
        drop(enq);

        let now = Reservation::open(h.asset, h.frame_in_cluster(3), 1, LoadMode::Now);
        assert!(h.is_ready_at(3), "load-now fills synchronously");
        drop(now);
    }

    /// `Enqueue` must schedule its freshly reserved clusters on the real loader queue, not only
    /// lease them.
    #[test]
    fn open_enqueue_schedules_covered_clusters_on_the_loader() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = TestHarness::with_audio_data_clusters(2..6, 4);
        h.evict_all(); // Every covered cluster is a fresh miss.

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
