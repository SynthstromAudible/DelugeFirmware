//! The `deluge_sample_source_*` / `deluge_sample_region_*` C ABI (`libdeluge/sample_source.h`).
//!
//! Wraps [`SampleSource<ManagerResidency>`](SampleSource) behind a fixed, allocation-free static
//! pool of cursors. The symbols are exported under the `c_abi` feature.
//!
//! # `open`'s `stream_backing`
//!
//! The reader passes its opaque `DelugeStreamBacking*` (a C++ `SampleStream`). This module never
//! dereferences it; it recovers the `{manager handle, asset id}` pair [`ManagerResidency`] needs
//! from [`deluge_streaming_resource_manager`] and [`deluge_sample_stream_asset_id`]
//! (`libdeluge/sample_stream.h`).
//!
//! # `retain` / `release` and the single manager
//!
//! `deluge_sample_region_retain`/`_release` take only the opaque `lease` token, which is meaningful
//! only relative to the manager that minted it. Production runs one boot-singleton manager, so
//! `open` caches its handle in `ACTIVE_MANAGER` and the lease-only calls route through it. More
//! than one manager would break this.

use core::cell::{Cell, UnsafeCell};
use core::ffi::c_void;

use crate::cursor::{RegionState, SampleSource};
use crate::geometry::{Geometry, UNKNOWN_LENGTH_SENTINEL};
use crate::manager_residency::ManagerResidency;
use deluge_resource::sync::{m_get, m_set, Masked};
use deluge_resource::{DelugeResource, Resource};

// C-ABI types, mirroring `libdeluge/sample_source.h`.

/// Opaque per-reader cursor handle, mirroring `DelugeSampleSource`.
///
/// Every non-null handle [`deluge_sample_source_open`] returns points at a pool `Slot`'s `source`
/// field.
#[repr(C)]
pub struct DelugeSampleSource {
    _opaque: [u8; 0],
}

/// A sample's stream as the C ABI names it (`DelugeStreamBacking`, `libdeluge/types.h`): the C++
/// `deluge::audio::stream::SampleStream`. Only the C++ side dereferences it.
#[repr(C)]
pub struct DelugeStreamBacking {
    _opaque: [u8; 0],
}

/// Residency outcome of a region query, mirroring `DelugeRegionState`.
///
/// Numbered from 1, so `if (state)` cannot be misread as a boolean. `#[repr(u8)]`, not
/// `#[repr(C)]`: the header fixes the underlying type to `uint8_t`, and the enum crosses the FFI
/// by value as [`deluge_sample_region_acquire_ex`]'s return.
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum DelugeRegionState {
    Ready = 1,
    Loading = 2,
    Unavailable = 3,
}

impl From<RegionState> for DelugeRegionState {
    fn from(s: RegionState) -> Self {
        match s {
            RegionState::Ready => DelugeRegionState::Ready,
            RegionState::Loading => DelugeRegionState::Loading,
            RegionState::Unavailable => DelugeRegionState::Unavailable,
        }
    }
}

/// One acquired, pinned region of resident sample data, mirroring `DelugeSampleRegion`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct DelugeSampleRegion {
    pub payload_base: *mut c_void,
    pub region_index: u32,
    pub resident_bytes: u32,
    pub lease: u64,
}

// `DelugeSampleGeometry` is `crate::geometry::Geometry`, already `#[repr(C)]`.

// The `open` bridge: stream backing -> {manager handle, asset id}.

unsafe extern "C" {
    /// The boot-singleton resource manager the streaming loader queue lives on. Untyped, as every
    /// crate declares it; the caller casts it to the manager.
    pub fn deluge_streaming_resource_manager() -> *mut c_void;
    /// `stream_backing`'s resource-manager asset id, defining the asset on first use.
    pub fn deluge_sample_stream_asset_id(stream_backing: *mut DelugeStreamBacking) -> u32;
}

// Static, allocation-free source pool.

/// Number of pooled cursors: `16 * kMaxNumVoicesUnison * kNumSources`.
///
/// `open` runs at note start on the audio tier, so it claims a static slot rather than touching
/// the heap. The size leaves headroom for a same-tick note-on burst before the CPU culler reclaims
/// voices.
const POOL_SIZE: usize = 256;

/// One pooled cursor and its claimed flag.
///
/// `source` is at offset 0 (asserted below), so the handle `open` returns, a pointer at `source`,
/// casts straight back to its `Slot`. `in_use` is claimed and released under
/// `deluge_resource::sync::Masked`.
#[repr(C)]
struct Slot {
    source: UnsafeCell<Option<SampleSource<ManagerResidency>>>,
    in_use: Cell<bool>,
}

impl Slot {
    const fn new() -> Self {
        Slot {
            source: UnsafeCell::new(None),
            in_use: Cell::new(false),
        }
    }
}

const _: () = assert!(
    core::mem::offset_of!(Slot, source) == 0,
    "deluge_sample_source_close recovers the enclosing Slot by reinterpreting \
     the DelugeSampleSource pointer straight back to *const Slot -- `source` \
     must stay Slot's first (offset-0) field for that cast to be sound"
);

// SAFETY: both fields are interior-mutable and every access is masked:
//   1. `in_use` is read-then-set (claim) and cleared (release) inside a `Masked` window. Only the
//      caller whose claim flipped it owns `source` until the matching close, so no two callers
//      alias a slot's `source`.
//   2. Once claimed, `source`'s contents are touched only through `SampleSource`'s own masked
//      cells.
// `open` and `close` run on the audio tier (a pooled reader reopened via
// `SampleLowLevelReader::ensureSource`) and in thread mode (voice teardown). `Masked` masks in
// every context but audio, and audio preempts the others, so each claim and release is
// indivisible with respect to the other contexts. This guards pool-slot integrity only: closing a
// source while another context still acquires through the same handle is the caller's bug.
unsafe impl Sync for Slot {}

/// The source pool, in `.bss`.
static POOL: [Slot; POOL_SIZE] = [const { Slot::new() }; POOL_SIZE];

/// The boot-singleton manager handle, cached by the most recent successful `open` for the
/// lease-only `retain`/`release` (see the module docs). Accessed only through [`m_get`]/[`m_set`].
struct ActiveManagerCell(Cell<*mut DelugeResource>);

// SAFETY: every access goes through `m_get`/`m_set`, which mask as for `Slot` above.
unsafe impl Sync for ActiveManagerCell {}

static ACTIVE_MANAGER: ActiveManagerCell = ActiveManagerCell(Cell::new(core::ptr::null_mut()));

/// Claims a free pool slot, or `None` if every slot is in use.
///
/// Each slot's check-and-set gets its own masked window, so the scan never holds the mask for
/// longer than one slot.
fn claim_slot() -> Option<&'static Slot> {
    POOL.iter().find(|slot| {
        let _m = Masked::enter();
        if slot.in_use.get() {
            false
        } else {
            slot.in_use.set(true);
            true
        }
    })
}

/// Borrows the cursor behind `src`, or `None` if `src` is null. `'static` because every handle
/// points into [`POOL`].
///
/// # Safety
///
/// `src`, if non-null, must come from [`deluge_sample_source_open`] and not yet be closed.
unsafe fn source_ref(
    src: *const DelugeSampleSource,
) -> Option<&'static SampleSource<ManagerResidency>> {
    if src.is_null() {
        return None;
    }
    // SAFETY: per this fn's contract, `src` points at a live slot's `source` cell.
    let cell = unsafe { &*(src as *const Option<SampleSource<ManagerResidency>>) };
    cell.as_ref()
}

/// Returns the file cluster count for `geo`: `ceil((audio_data_start_bytes +
/// audio_data_length_bytes) / cluster_size_bytes)`.
///
/// Region indices are file cluster indices, so a header that pushes the audio's tail past a
/// boundary adds a cluster. A zero length or the unknown-length sentinel is unbounded
/// (`u32::MAX`), matching [`resident_bytes_for`](crate::geometry::resident_bytes_for); a zero
/// cluster size gives `0`.
fn num_clusters_for(geo: &Geometry) -> u32 {
    if geo.cluster_size_bytes == 0 {
        return 0;
    }
    if geo.audio_data_length_bytes == 0 || geo.audio_data_length_bytes == UNKNOWN_LENGTH_SENTINEL {
        return u32::MAX;
    }
    (geo.audio_data_start_bytes as u64 + geo.audio_data_length_bytes)
        .div_ceil(geo.cluster_size_bytes as u64)
        .min(u32::MAX as u64) as u32
}

// C-ABI surface.

/// Opens a per-reader cursor over `stream_backing`.
///
/// Returns null if `stream_backing` is null, no manager exists yet, or the pool is exhausted.
///
/// # Safety
///
/// `stream_backing`, if non-null, must be a live `SampleStream` for the duration of the call. The
/// handle [`deluge_streaming_resource_manager`] returns, if non-null, must stay live for the
/// cursor's whole life.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_source_open(
    stream_backing: *mut DelugeStreamBacking,
    geometry: Geometry,
) -> *mut DelugeSampleSource {
    if stream_backing.is_null() {
        return core::ptr::null_mut();
    }
    // SAFETY: a getter with no preconditions.
    let handle = unsafe { deluge_streaming_resource_manager() }.cast::<DelugeResource>();
    if handle.is_null() {
        // No manager yet (boot ordering).
        return core::ptr::null_mut();
    }
    // SAFETY: `stream_backing` is non-null and live per this fn's contract.
    let asset = unsafe { deluge_sample_stream_asset_id(stream_backing) };
    let num_clusters = num_clusters_for(&geometry);
    // SAFETY: `handle` is non-null and, per this fn's contract, live for the cursor's life.
    let residency = unsafe {
        ManagerResidency::new(
            handle,
            asset,
            geometry.cluster_size_bytes as usize,
            num_clusters,
        )
    };
    let source = SampleSource::new(residency, geometry);

    let Some(slot) = claim_slot() else {
        // Pool exhausted: return null and let the caller drop the voice, as `deluge_resource`
        // does for its own full tables.
        return core::ptr::null_mut();
    };
    // SAFETY: `claim_slot` just claimed `slot` for this caller alone, so this write is exclusive.
    unsafe {
        *slot.source.get() = Some(source);
    }
    // Cache the handle for the lease-only `retain`/`release` (see the module docs).
    m_set(&ACTIVE_MANAGER.0, handle);
    slot.source.get() as *mut DelugeSampleSource
}

/// Makes region `index` resident or scheduled, pins it, and reports which (see
/// [`SampleSource::acquire_ex`]).
///
/// `out` is written only on `Ready`. A null `src` (an `open` that failed) is `Unavailable`.
///
/// # Safety
///
/// `src`, if non-null, must come from [`deluge_sample_source_open`] and not yet be closed. `out`,
/// if non-null, must be valid for writes.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_region_acquire_ex(
    src: *mut DelugeSampleSource,
    index: u32,
    direction: i8,
    priority: u32,
    out: *mut DelugeSampleRegion,
) -> DelugeRegionState {
    // SAFETY: `source_ref` null-checks; a non-null `src` is live per this fn's contract.
    let Some(source) = (unsafe { source_ref(src as *const DelugeSampleSource) }) else {
        return DelugeRegionState::Unavailable;
    };
    let (state, region) = source.acquire_ex(index, direction, priority);
    if let (RegionState::Ready, Some(region)) = (state, region) {
        if !out.is_null() {
            // SAFETY: `out` is non-null and valid for writes per this fn's contract.
            unsafe {
                *out = DelugeSampleRegion {
                    payload_base: region.payload_base as *mut c_void,
                    region_index: region.region_index,
                    resident_bytes: region.resident_bytes,
                    lease: region.lease,
                };
            }
        }
    }
    state.into()
}

/// Boolean form of [`deluge_sample_region_acquire_ex`]: `true` on `Ready`.
///
/// # Safety
///
/// As for [`deluge_sample_region_acquire_ex`].
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_region_acquire(
    src: *mut DelugeSampleSource,
    index: u32,
    direction: i8,
    priority: u32,
    out: *mut DelugeSampleRegion,
) -> bool {
    // SAFETY: this fn's contract is `acquire_ex`'s.
    (unsafe { deluge_sample_region_acquire_ex(src, index, direction, priority, out) })
        == DelugeRegionState::Ready
}

/// Returns the residency of `index` as the cursor tracks it now, without acquiring anything
/// (see [`SampleSource::state`]). A null `src` is `Unavailable`.
///
/// # Safety
///
/// `src`, if non-null, must come from [`deluge_sample_source_open`] and not yet be closed.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_region_state(
    src: *const DelugeSampleSource,
    index: u32,
) -> DelugeRegionState {
    // SAFETY: `source_ref` null-checks; a non-null `src` is live per this fn's contract.
    match unsafe { source_ref(src) } {
        Some(source) => source.state(index).into(),
        None => DelugeRegionState::Unavailable,
    }
}

/// Takes an independent pin on a region's chunk by its `lease` token.
///
/// A no-op on `0`, a stale token, or before any source has been opened.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub extern "C" fn deluge_sample_region_retain(lease: u64) {
    let handle = m_get(&ACTIVE_MANAGER.0);
    if handle.is_null() {
        return;
    }
    // SAFETY: a successful `open` stored `handle`; the boot-singleton manager stays live.
    let res = unsafe { Resource::from_handle(handle) };
    res.retain_token(lease);
}

/// Drops an independent pin taken by [`deluge_sample_region_retain`], with the same no-op cases.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub extern "C" fn deluge_sample_region_release(lease: u64) {
    let handle = m_get(&ACTIVE_MANAGER.0);
    if handle.is_null() {
        return;
    }
    // SAFETY: see deluge_sample_region_retain.
    let res = unsafe { Resource::from_handle(handle) };
    res.release_token(lease);
}

/// Closes the cursor, releasing every lease it holds, and returns its pool slot. A no-op on null.
///
/// # Safety
///
/// `src`, if non-null, must come from [`deluge_sample_source_open`] and not yet be closed; it must
/// not be used after this call.
#[cfg_attr(feature = "c_abi", unsafe(no_mangle))]
pub unsafe extern "C" fn deluge_sample_source_close(src: *mut DelugeSampleSource) {
    if src.is_null() {
        return;
    }
    // SAFETY: `src` points at a slot's `source`, which is at offset 0 of the `#[repr(C)]` `Slot`
    // (asserted above), so the same address is the slot.
    let slot = unsafe { &*(src as *const Slot) };
    // SAFETY: the caller owns the slot and, per this fn's contract, uses `src` no more, so nothing
    // else touches `slot.source` (see `Slot`'s `Sync` note).
    let cell = unsafe { &mut *slot.source.get() };
    if let Some(source) = cell.as_ref() {
        source.close();
    }
    *cell = None;
    // Masked release, pairing with `claim_slot`'s masked claim.
    m_set(&slot.in_use, false);
}

// Tests of the C ABI and its pool over a real manager and real streamed-chunk backing.
#[cfg(test)]
mod tests {
    use super::*;
    extern crate std;

    use deluge_resource::value::COST_IO;
    use std::sync::Mutex;
    use std::vec::Vec;

    const CLUSTER_SIZE: usize = 16;

    /// Per-slot slab size: the `StreamedChunk` header plus a `CLUSTER_SIZE` payload, 16-aligned,
    /// as production's slab slots are sized.
    const BACKING_SIZE: usize =
        (deluge_sample_fill::chunk::RUST_CHUNK_PAYLOAD_OFFSET + CLUSTER_SIZE).next_multiple_of(16);

    /// Serializes the tests, which share the process-wide [`POOL`], [`ACTIVE_MANAGER`] and
    /// `TEST_BRIDGE`.
    static TEST_LOCK: Mutex<()> = Mutex::new(());

    /// The production streamed-chunk construct callback, adapted to `ConstructFn`'s `*mut u8`
    /// `dest`.
    ///
    /// # Safety
    ///
    /// `dest` must be a writable slab slot of at least `BACKING_SIZE` bytes.
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

    /// The arena behind a leaked test heap.
    struct TestHeap {
        _buf: Vec<u128>,
    }

    /// Builds a manager with a `BACKING_SIZE`-slotted slab over a fresh, leaked test heap. Streamed
    /// chunks are slab-backed in production.
    fn test_manager_handle() -> *mut DelugeResource {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf: Vec<u128> = std::vec![0u128; words];
        let ptr = buf.as_mut_ptr() as *mut u8;
        // SAFETY: `buf` is leaked below so the arena stays alive for the whole
        // test binary's life, matching `ManagerResidency::new`'s boot-singleton
        // contract.
        let h = unsafe { deluge_alloc::deluge_heap_create(ptr, words * 16) };
        std::mem::forget(TestHeap { _buf: buf });
        // SAFETY: `h` is the live heap handle just created above.
        let slab = unsafe { deluge_alloc::slab::deluge_slab_create_unmanaged(h, BACKING_SIZE, 16) };
        assert!(!slab.is_null());
        // SAFETY: `h` is the live heap handle just created above.
        let handle = unsafe { deluge_resource::deluge_resource_create(h, 4, 16) };
        assert!(!handle.is_null());
        // SAFETY: `handle`/`slab` are both live, over the same heap.
        unsafe { deluge_resource::deluge_resource_set_slab(handle, slab) };
        handle
    }

    /// Defines a slab-backed streamed-chunk asset, with no `materialize`, as production does.
    fn define_ramp_asset(h: *mut DelugeResource) -> u32 {
        // SAFETY: `h` is a live handle (from `test_manager_handle`); the
        // callback has the required C-ABI signature.
        let asset = unsafe {
            deluge_resource::deluge_resource_define_asset(
                h,
                core::ptr::null_mut(),
                None,
                None,
                core::ptr::null_mut(),
                COST_IO,
                deluge_resource::manager::BACKING_SLAB,
            )
        };
        // SAFETY: `h`/`asset` are live/valid per the call above.
        unsafe {
            deluge_resource::deluge_resource_set_construct(h, asset, Some(real_chunk_construct));
        }
        asset
    }

    /// Marks `index` ready through the manager directly, standing in for the loader, after seeding
    /// its payload with the ramp `payload[b] = index + b`.
    fn mark_index_ready(h: *mut DelugeResource, asset: u32, index: u32) {
        // SAFETY: `h` is a live handle; these are the same FFI-safe C-ABI
        // calls the facade wraps.
        let ptr = unsafe { deluge_resource::deluge_resource_try_acquire(h, asset, index) };
        let ptr = if ptr.is_null() {
            // SAFETY: `h`/`asset` are live/valid.
            let p =
                unsafe { deluge_resource::deluge_resource_request(h, asset, index, CLUSTER_SIZE) };
            assert!(!p.is_null(), "reserve for mark-ready failed");
            p
        } else {
            ptr
        };
        // SAFETY: `ptr` is a live chunk backing `real_chunk_construct` already placement-wrote a
        // `StreamedChunk` into (via `request`/`try_acquire` above).
        let payload = unsafe { deluge_sample_fill::chunk::payload(ptr as *mut c_void) };
        for b in 0..CLUSTER_SIZE {
            // SAFETY: `payload` is the chunk's own `CLUSTER_SIZE`-byte payload region.
            unsafe {
                *payload.add(b) = index.wrapping_add(b as u32) as u8;
            }
        }
        // SAFETY: `h`/`ptr` are live/valid per the calls above.
        unsafe { deluge_resource::deluge_resource_mark_ready(h, ptr) };
        // SAFETY: `h`/`ptr` are live/valid.
        unsafe { deluge_resource::deluge_resource_release(h, ptr) };
    }

    /// A quarter-cluster header before exactly two clusters of audio spans three file clusters, the
    /// third holding the last quarter: a WAV's 44-byte header before 64 KiB of audio, at 32 KiB
    /// clusters.
    #[test]
    fn num_clusters_counts_the_header_offset() {
        let geo = Geometry {
            audio_data_start_bytes: (CLUSTER_SIZE / 4) as u32,
            audio_data_length_bytes: 2 * CLUSTER_SIZE as u64,
            cluster_size_bytes: CLUSTER_SIZE as u32,
            byte_depth: 2,
            num_channels: 1,
            raw_data_format: 0,
        };
        assert_eq!(num_clusters_for(&geo), 3);
        assert_eq!(
            crate::geometry::resident_bytes_for(2, &geo),
            (CLUSTER_SIZE / 4) as u32
        );
        // No offset: exactly the audio's own clusters.
        let flush = Geometry {
            audio_data_start_bytes: 0,
            ..geo
        };
        assert_eq!(num_clusters_for(&flush), 2);
    }

    fn test_geo(num_clusters: u32) -> Geometry {
        Geometry {
            audio_data_start_bytes: 0,
            audio_data_length_bytes: (num_clusters as u64) * (CLUSTER_SIZE as u64),
            cluster_size_bytes: CLUSTER_SIZE as u32,
            byte_depth: 2,
            num_channels: 1,
            raw_data_format: 0,
        }
    }

    /// What the test definitions of `deluge_streaming_resource_manager` and
    /// `deluge_sample_stream_asset_id` return. The app's real definitions are not linked, and a
    /// unit test has no real `SampleStream`. Set by `set_test_bridge` under `TEST_LOCK`.
    struct TestBridge {
        handle: Cell<*mut DelugeResource>,
        asset: Cell<u32>,
    }
    // SAFETY: every access is serialized by `TEST_LOCK`.
    unsafe impl Sync for TestBridge {}
    static TEST_BRIDGE: TestBridge = TestBridge {
        handle: Cell::new(core::ptr::null_mut()),
        asset: Cell::new(0),
    };

    fn set_test_bridge(handle: *mut DelugeResource, asset: u32) {
        TEST_BRIDGE.handle.set(handle);
        TEST_BRIDGE.asset.set(asset);
    }

    /// Test definition of `deluge_streaming_resource_manager`.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_streaming_resource_manager() -> *mut DelugeResource {
        TEST_BRIDGE.handle.get()
    }

    /// Test definition of `deluge_sample_stream_asset_id`; ignores `stream_backing`.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_sample_stream_asset_id(_stream_backing: *mut DelugeStreamBacking) -> u32 {
        TEST_BRIDGE.asset.get()
    }

    /// A non-null `stream_backing` that nothing dereferences.
    fn dummy_stream_backing() -> *mut DelugeStreamBacking {
        static DUMMY: u8 = 0;
        &DUMMY as *const u8 as *mut DelugeStreamBacking
    }

    #[test]
    fn open_close_pool_is_alloc_free_and_balanced() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = test_manager_handle();
        let asset = define_ramp_asset(h);
        let geo = test_geo(4);
        set_test_bridge(h, asset);
        let backing = dummy_stream_backing();

        // SAFETY: `backing` is a live dummy for the whole test; `geo` is Copy.
        let src1 = unsafe { deluge_sample_source_open(backing, geo) };
        assert!(!src1.is_null(), "first open must succeed");

        // A second open gets a different slot from the first, still-live one.
        let src2 = unsafe { deluge_sample_source_open(backing, geo) };
        assert!(!src2.is_null(), "second open must succeed");
        assert_ne!(
            src1, src2,
            "two concurrently open sources must claim distinct slots"
        );

        // SAFETY: `src1`/`src2` are live, not yet closed.
        unsafe {
            deluge_sample_source_close(src1);
            deluge_sample_source_close(src2);
        }

        // Further open/close cycles keep succeeding, so close returns slots to the pool.
        for _ in 0..8 {
            // SAFETY: `backing` is still live.
            let s = unsafe { deluge_sample_source_open(backing, geo) };
            assert!(
                !s.is_null(),
                "close must return the slot to the pool for reuse"
            );
            // SAFETY: `s` is live, not yet closed.
            unsafe { deluge_sample_source_close(s) };
        }
    }

    #[test]
    fn abi_acquire_ex_matches_cursor_over_ready_index() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = test_manager_handle();
        let asset = define_ramp_asset(h);
        mark_index_ready(h, asset, 0);
        let geo = test_geo(4);
        set_test_bridge(h, asset);
        let backing = dummy_stream_backing();

        // SAFETY: `backing` is live for the whole test; `geo` is Copy.
        let src = unsafe { deluge_sample_source_open(backing, geo) };
        assert!(!src.is_null());

        let mut out = DelugeSampleRegion {
            payload_base: core::ptr::null_mut(),
            region_index: 0,
            resident_bytes: 0,
            lease: 0,
        };
        // SAFETY: `src` is live; `out` is a valid writable local.
        let state = unsafe { deluge_sample_region_acquire_ex(src, 0, 1, 0, &mut out) };
        assert_eq!(state, DelugeRegionState::Ready);
        assert!(!out.payload_base.is_null(), "READY must fill payload_base");
        assert_eq!(out.resident_bytes, CLUSTER_SIZE as u32);
        assert_ne!(out.lease, 0, "READY must mint a non-zero lease token");

        // payload_base = the seeded ramp for index 0: dest[b] = 0 + b.
        for b in 0..CLUSTER_SIZE {
            // SAFETY: `payload_base` is the manager's live, ready, leased
            // backing for cluster 0, at least `CLUSTER_SIZE` bytes.
            let byte = unsafe { *(out.payload_base as *const u8).add(b) };
            assert_eq!(byte, b as u8, "ramp byte {b} of cluster 0");
        }

        // retain/release route through `ACTIVE_MANAGER`.
        deluge_sample_region_retain(out.lease);
        deluge_sample_region_release(out.lease);

        // SAFETY: `src` is live, not yet closed.
        unsafe { deluge_sample_source_close(src) };
    }

    /// The boolean [`deluge_sample_region_acquire`] is `true` with the same region on `Ready`, and
    /// `false` with `out` untouched otherwise.
    #[test]
    fn boolean_acquire_matches_ready_and_is_false_with_out_untouched_otherwise() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let h = test_manager_handle();
        let asset = define_ramp_asset(h);
        mark_index_ready(h, asset, 0);
        let geo = test_geo(4);
        set_test_bridge(h, asset);
        let backing = dummy_stream_backing();

        // SAFETY: `backing` is live for the whole test; `geo` is Copy.
        let src = unsafe { deluge_sample_source_open(backing, geo) };
        assert!(!src.is_null());

        let mut via_ex = DelugeSampleRegion {
            payload_base: core::ptr::null_mut(),
            region_index: 0,
            resident_bytes: 0,
            lease: 0,
        };
        // SAFETY: `src` is live; `via_ex` is a valid writable local.
        let state = unsafe { deluge_sample_region_acquire_ex(src, 0, 1, 0, &mut via_ex) };
        assert_eq!(state, DelugeRegionState::Ready);

        // A second cursor, so the boolean call is a fresh acquire.
        // SAFETY: `backing` is still live.
        let src_bool = unsafe { deluge_sample_source_open(backing, geo) };
        assert!(!src_bool.is_null());
        let mut via_bool = DelugeSampleRegion {
            payload_base: core::ptr::null_mut(),
            region_index: 0,
            resident_bytes: 0,
            lease: 0,
        };
        // SAFETY: `src_bool` is live; `via_bool` is a valid writable local.
        let ok = unsafe { deluge_sample_region_acquire(src_bool, 0, 1, 0, &mut via_bool) };
        assert!(ok, "boolean acquire must be true on READY");
        assert_eq!(via_bool.payload_base, via_ex.payload_base);
        assert_eq!(via_bool.region_index, via_ex.region_index);
        assert_eq!(via_bool.resident_bytes, via_ex.resident_bytes);
        assert_ne!(via_bool.lease, 0);

        // LOADING (index 1 is never marked ready): false, `out` left untouched.
        let mut probe = DelugeSampleRegion {
            payload_base: core::ptr::null_mut(),
            region_index: 999,
            resident_bytes: 999,
            lease: 999,
        };
        // SAFETY: `src_bool` is live; `probe` is a valid writable local.
        let ok = unsafe { deluge_sample_region_acquire(src_bool, 1, 1, 0, &mut probe) };
        assert!(!ok, "boolean acquire must be false on LOADING");
        assert!(
            probe.payload_base.is_null(),
            "LOADING must leave out untouched"
        );
        assert_eq!(probe.region_index, 999);
        assert_eq!(probe.resident_bytes, 999);
        assert_eq!(probe.lease, 999);

        // UNAVAILABLE (out of range): false, `out` left untouched.
        let mut probe2 = DelugeSampleRegion {
            payload_base: core::ptr::null_mut(),
            region_index: 999,
            resident_bytes: 999,
            lease: 999,
        };
        // SAFETY: `src` is live; `probe2` is a valid writable local.
        let ok = unsafe { deluge_sample_region_acquire(src, 100, 1, 0, &mut probe2) };
        assert!(!ok, "boolean acquire must be false on UNAVAILABLE");
        assert!(
            probe2.payload_base.is_null(),
            "UNAVAILABLE must leave out untouched"
        );
        assert_eq!(probe2.region_index, 999);

        // SAFETY: both are live, not yet closed.
        unsafe {
            deluge_sample_source_close(src);
            deluge_sample_source_close(src_bool);
        }
    }

    #[test]
    fn acquire_ex_with_null_src_is_unavailable_and_does_not_deref() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        let mut out = DelugeSampleRegion {
            payload_base: core::ptr::null_mut(),
            region_index: 0,
            resident_bytes: 0,
            lease: 0,
        };
        // SAFETY: `src` is null (the case under test); `out` is a valid
        // writable local.
        let state =
            unsafe { deluge_sample_region_acquire_ex(core::ptr::null_mut(), 0, 1, 0, &mut out) };
        assert_eq!(state, DelugeRegionState::Unavailable);
        assert!(
            out.payload_base.is_null(),
            "out must be left untouched on UNAVAILABLE"
        );
    }

    #[test]
    fn null_and_zero_tolerant_everywhere_else() {
        let _lock = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        // state() on a null src -> UNAVAILABLE, no deref.
        // SAFETY: `src` is null (the case under test).
        assert_eq!(
            unsafe { deluge_sample_region_state(core::ptr::null(), 0) },
            DelugeRegionState::Unavailable
        );
        // close() on a null src and retain/release on a zero token are no-ops.
        // SAFETY: `src` is null (the case under test).
        unsafe { deluge_sample_source_close(core::ptr::null_mut()) };
        deluge_sample_region_retain(0);
        deluge_sample_region_release(0);
    }
}
