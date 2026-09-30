//! The `deluge_sample_source_*` / `deluge_sample_region_*` C ABI
//! (`include/libdeluge/sample_source.h`), wrapping `cursor::SampleSource<
//! ManagerResidency>` behind a fixed, allocation-free static source pool --
//! reimplements `sample_source.cpp`'s pool discipline (`g_source_pool`,
//! `claim_source_slot`/`release_source_slot`) natively.
//!
//! The link-time weak/strong selector that makes these symbols win over the
//! C++ backing is the `cfg_attr` gate on each `#[no_mangle]` below (device and
//! host_app build it directly; the C-host sim links it via this crate's own
//! `sim` feature, forwarded from `deluge_rust`'s umbrella build -- see
//! `crates/deluge_rust/Cargo.toml` and `sim/CMakeLists.txt`).
//!
//! # `open()`'s `stream_backing`
//! The reader passes its opaque `deluge::audio::stream::SampleStream*`
//! unchanged -- the same pointer the C++ backing
//! (`sample_source.cpp`) already casts to `SampleStream*`. This module
//! bridges it to the `{manager handle, asset id}` pair `ManagerResidency`
//! needs via two C-ABI calls: `deluge_streaming_resource_manager()` (the
//! process-wide boot-singleton getter, `include/libdeluge/sample_stream.h`)
//! and `deluge_sample_stream_asset_id(stream_backing)` (the lazy-init
//! accessor added alongside it, wrapping `deluge_streaming_define_asset()`).
//! Neither call dereferences `stream_backing` on the Rust side -- it is
//! forwarded straight through as the header's opaque `DelugeStreamBacking*`. Unit tests can't build a real `SampleStream`,
//! so they override both externs with test doubles driving a test
//! `deluge_resource` manager + asset (see the `tests` module below).
//!
//! # `retain`/`release` and the single boot-singleton manager
//! `deluge_sample_region_retain`/`_release` take ONLY the opaque `lease`
//! token -- no source, no manager handle -- matching the header exactly. That
//! works in the C++ backing because a lease there IS a raw `StreamedChunk*`,
//! self-sufficient with no external table lookup. `ManagerResidency`'s token
//! is `{slot<<32 | gen}`, meaningful only relative to the specific
//! `*mut DelugeResource` that minted it, so a lease-only call needs SOME way
//! to recover that handle. Production runs exactly one boot-singleton
//! `deluge_resource` manager (the same contract `ManagerResidency::new`
//! documents), so this module caches the most-recently-`open()`-ed handle in
//! [`ACTIVE_MANAGER`] and routes standalone retain/release through it --
//! correct for the current single-manager reality, and it preserves the
//! header's exact signature (no added parameter). Flagged here for whoever
//! revisits a hypothetical multi-manager future.

use core::cell::{Cell, UnsafeCell};
use core::ffi::c_void;

use crate::cursor::{RegionState, SampleSource};
use crate::geometry::{Geometry, UNKNOWN_LENGTH_SENTINEL};
use crate::manager_residency::ManagerResidency;
use deluge_resource::sync::{m_get, m_set, Masked};
use deluge_resource::{DelugeResource, Resource};

// ---------------------------------------------------------------------------
// C-ABI type reprs (field-for-field mirrors of include/libdeluge/sample_source.h)
// ---------------------------------------------------------------------------

/// Opaque per-reader cursor handle, mirroring `DelugeSampleSource`. Owned by
/// this module: every non-null pointer of this type that ever leaves `open()`
/// actually points at a pool [`Slot`]'s `source` field (see `open`/`close`).
#[repr(C)]
pub struct DelugeSampleSource {
    _opaque: [u8; 0],
}

/// A sample's stream as the C ABI names it (`DelugeStreamBacking`, `include/libdeluge/types.h`):
/// the C++ `deluge::audio::stream::SampleStream`. Only the C++ side dereferences it; this crate
/// passes it back to `deluge_sample_stream_asset_id`.
#[repr(C)]
pub struct DelugeStreamBacking {
    _opaque: [u8; 0],
}

/// Residency outcome of a region query, mirroring `DelugeRegionState`.
/// Numbered from 1 (never 0), matching the header, so `if (state)` can't be
/// misread as a boolean.
///
/// `#[repr(u8)]`, NOT plain `#[repr(C)]`: the header (`include/libdeluge/
/// sample_source.h`) pins `enum DelugeRegionState` to an explicit `: uint8_t`
/// fixed underlying type (C23 + C++11 syntax) -- like every other libdeluge
/// enum, each pinned to its own explicit fixed width in its header. This one
/// crosses the FFI BY VALUE (`deluge_sample_region_acquire_ex`'s return), so
/// a bare `#[repr(C)]` enum (Rust's C `int`, 4 bytes) would mismatch the
/// header's 1-byte width; `#[repr(u8)]` matches it exactly. This does NOT
/// rely on `-fshort-enums` -- the build passes no such flag (see
/// `build.rs`'s `run_bindgen`); the explicit underlying type is what fixes
/// the width on both sides, independent of any compiler flag.
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

/// One acquired, pinned, borrowed region of resident sample data, mirroring
/// `DelugeSampleRegion`.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct DelugeSampleRegion {
    pub payload_base: *mut c_void,
    pub region_index: u32,
    pub resident_bytes: u32,
    pub lease: u64,
}

// `DelugeSampleGeometry` is NOT redefined here: `geometry::Geometry` is already `#[repr(C)]` and
// field-for-field identical to the header's `DelugeSampleGeometry` (see `geometry.rs`), so
// `deluge_sample_source_open` takes it directly rather than duplicating an equivalent struct.

// ---------------------------------------------------------------------------
// The open() bridge: stream_backing -> {manager handle, asset id}
// ---------------------------------------------------------------------------

unsafe extern "C" {
    /// The resource-manager instance the streaming loader queue lives on
    /// (`include/libdeluge/sample_stream.h`). The process-wide boot-singleton
    /// handle -- the same one `ManagerResidency::new`'s contract requires to
    /// stay live for the process's whole remaining life. Untyped, the one spelling every crate that declares it
    /// shares; the caller casts it to the manager.
    pub fn deluge_streaming_resource_manager() -> *mut c_void;
    /// `stream_backing`'s resource-manager Asset id, defining it first if not
    /// yet defined (`include/libdeluge/sample_stream.h`, wrapping
    /// `deluge_streaming_define_asset()`).
    pub fn deluge_sample_stream_asset_id(stream_backing: *mut DelugeStreamBacking) -> u32;
}

// ---------------------------------------------------------------------------
// Static, allocation-free source pool
// ---------------------------------------------------------------------------

/// Fixed source pool -- `open()` claims a slot under a masked check-and-set
/// (allocation-free, ISR-safe), never the heap: `open` fires at note-start on
/// the audio render thread, so it must not route through a heap that can
/// walk/lock/throw. Sized like the C++ pool: `16 * kMaxNumVoicesUnison *
/// kNumSources == 256` (see `sample_source.cpp`'s derivation -- a 96-reader
/// concurrent-open ceiling at default pool sizing, ~2.6x headroom for a
/// same-tick note-on burst before the CPU culler reclaims voices).
const POOL_SIZE: usize = 256;

/// One pooled cursor plus its claimed/free marker. `source` is `Slot`'s FIRST
/// field (enforced by the `offset_of!` assertion below) so the
/// `*mut DelugeSampleSource` `open()` hands out -- a pointer straight at
/// `source` -- casts back to its enclosing `Slot` in O(1), no scan, mirroring
/// `sample_source.cpp`'s `SampleSourceSlot` (`source` first,
/// `static_assert(offsetof(..., source) == 0)`). `in_use` is a plain,
/// non-atomic `Cell<bool>` -- its claim/release are guarded by
/// `deluge_resource::sync::Masked`, the SAME primitive `SampleSource`'s own
/// slots and the manager's tables use (see `claim_slot`/`deluge_sample_source_close`
/// and this type's `Sync` doc below), not a separate atomic mechanism.
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

// SAFETY: `Slot`'s two fields, `source: UnsafeCell<...>` and
// `in_use: Cell<bool>`, are both interior-mutable and neither is `Sync` on its
// own; sharing `&Slot` across threads is sound because EVERY touch of EITHER
// field goes through the crate-wide masked-`Cell` discipline
// (`deluge_resource::sync::Masked`), never a separate atomic:
//   1. Slot INTEGRITY (which caller, if any, owns this slot's `source` right
//      now) is guarded by `in_use`, claimed/released only under a
//      `Masked::enter()` window (`claim_slot`, `deluge_sample_source_close`).
//      Whichever caller's masked read-then-set observes `in_use == false` and
//      flips it to `true` is the only caller ever permitted to read/write
//      `source` between its claim and the matching `close()`'s release, so two
//      callers never alias the same slot's `source`.
//   2. Every touch of `source`'s CONTENTS, once claimed, goes through
//      `SampleSource`'s own masked-`Cell` discipline (see `cursor.rs`'s module
//      doc) -- the identical mechanism, not a second one.
// The correctness model behind (1) does NOT depend on which thread runs which
// entry point. `open()` and `close()` can BOTH be invoked from the audio render
// thread -- a pooled reader reused for a new sample calls `close()` then
// `open()` on the render path via `SampleLowLevelReader::ensureSource()` -- as
// well as from the main thread (voice teardown / song swap). Soundness comes
// instead from `Masked::enter()` adapting to the CALLER's context through a live
// `deluge_in_interrupt()` check: called from the main thread it masks the audio
// ISR for the whole masked read-then-set (claim) or flip (release); called from
// the audio ISR it is a no-op, which is correct precisely because the audio ISR
// is atomic w.r.t. main (non-reentrant, un-preemptible by main). So a claim's
// masked read-then-set and a release's masked flip are each indivisible w.r.t.
// any pool operation in the OTHER context, whichever thread each actually runs
// on. Two claims never race each other (the audio ISR is single-threaded/
// non-reentrant, and a main-thread claim masks the ISR); a claim and a release
// on DIFFERENT slots never touch the same `in_use`; on the SAME slot they are
// serialized by that flag. A concurrent `acquire_ex` against an already-claimed
// slot's `source` stays coherent via (2). This protects POOL SLOT INTEGRITY
// only -- tearing down a source while something else still actively acquires
// through the SAME pointer is a pre-existing, out-of-scope hazard mirrored from
// the C++ backing (caller discipline, not this pool's job).
unsafe impl Sync for Slot {}

/// File-scope static: lives in `.bss`, never touches the heap.
static POOL: [Slot; POOL_SIZE] = [const { Slot::new() }; POOL_SIZE];

/// The single boot-singleton `deluge_resource` handle, cached from the most
/// recent successful `open()` -- see the module doc's `retain`/`release`
/// section for why the lease-only ABI needs this. A masked `Cell` (via
/// [`m_get`]/[`m_set`]), the SAME discipline [`Slot`]'s own `in_use` and
/// `SampleSource`'s slots use -- ONE concurrency mechanism for the whole
/// crate, not a second atomic just for this handle.
struct ActiveManagerCell(Cell<*mut DelugeResource>);

// SAFETY: the sole field is an interior-mutable `Cell<*mut DelugeResource>`,
// not `Sync` on its own; every touch (`open`'s writer, `retain`/`release`'s
// readers) goes through `deluge_resource::sync::m_get`/`m_set`, which wrap the
// access in the same masked critical section `Slot`'s `in_use` and
// `SampleSource`'s slots use, so concurrent access from the audio ISR and the
// main thread stays coherent by the same argument as `Slot`'s `Sync` doc
// above.
unsafe impl Sync for ActiveManagerCell {}

static ACTIVE_MANAGER: ActiveManagerCell = ActiveManagerCell(Cell::new(core::ptr::null_mut()));

/// Claim a free pool slot under a masked check-and-set, or `None` if every
/// slot is in use. Mirrors `sample_source.cpp`'s `claim_source_slot`.
///
/// Each slot's read-then-set runs in its OWN `Masked::enter()` window (not one
/// window over the whole scan), matching `SampleSource`'s own per-cell masking
/// granularity (see `cursor.rs`'s `set_slot`/`slot_index`) -- a scan that held
/// the mask for its entire length would mask the audio ISR far longer than any
/// single slot's critical section needs to.
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

/// Shared borrow of the `SampleSource` a live `src` pointer (from a prior
/// `open()`) tracks, or `None` if `src` is null. `'static` because every
/// non-null `src` this ABI ever hands out points into [`POOL`], a `'static`
/// static.
///
/// # Safety
/// `src`, if non-null, must be a pointer previously returned by
/// `deluge_sample_source_open` and not yet passed to
/// `deluge_sample_source_close` (the C-ABI's own caller contract -- mirrored,
/// not re-litigated, here).
unsafe fn source_ref(
    src: *const DelugeSampleSource,
) -> Option<&'static SampleSource<ManagerResidency>> {
    if src.is_null() {
        return None;
    }
    // SAFETY: forwarded from this fn's own contract above -- `src` points at a
    // live slot's `source` cell.
    let cell = unsafe { &*(src as *const Option<SampleSource<ManagerResidency>>) };
    cell.as_ref()
}

/// Total FILE cluster count for `geo` (`ceil((audio_data_start_bytes +
/// audio_data_length_bytes) / cluster_size_bytes)`): a region index is a file
/// cluster index, as `resident_bytes_for` counts it, so a header that pushes the
/// audio's tail past a boundary adds a cluster. Used only to size `ManagerResidency`'s
/// `num_clusters` range guard at `open()` time -- NOT the per-cluster
/// short-last-cluster byte accounting, which stays exclusively
/// `geometry::resident_bytes_for`'s job (this function does not re-derive
/// that math). Under the unknown-length sentinel or a zero length,
/// `resident_bytes_for` reports every cluster as fully valid rather than
/// clamping -- mirror that here by reporting the cluster range as unbounded
/// (`u32::MAX`) instead of a length computed from a not-yet-final byte count.
/// `cluster_size_bytes == 0` is degenerate (no valid geometry can divide by
/// it); report zero clusters rather than panicking on the division.
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

// ---------------------------------------------------------------------------
// C-ABI surface
// ---------------------------------------------------------------------------

/// Open a per-reader cursor from the reader's opaque stream backing. See the
/// module doc for the `stream_backing` -> `{manager handle, asset id}` bridge.
///
/// # Safety
/// `stream_backing`, if non-null, must be a live `deluge::audio::stream::
/// SampleStream*` for the duration of this call -- forwarded as-is to
/// `deluge_sample_stream_asset_id`, never dereferenced on the Rust side.
/// `deluge_streaming_resource_manager`'s returned handle, if non-null, must be
/// live for the whole life of the `SampleSource` this opens (the crate-wide
/// boot-singleton contract -- see `ManagerResidency::new`).
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub unsafe extern "C" fn deluge_sample_source_open(
    stream_backing: *mut DelugeStreamBacking,
    geometry: Geometry,
) -> *mut DelugeSampleSource {
    if stream_backing.is_null() {
        return core::ptr::null_mut();
    }
    // SAFETY: no preconditions beyond FFI-safety -- the process-wide
    // boot-singleton getter (mirrors `GeneralMemoryAllocator::get().resourceManager()`).
    let handle = unsafe { deluge_streaming_resource_manager() }.cast::<DelugeResource>();
    if handle.is_null() {
        // No manager yet (e.g. boot ordering) -- nothing to build a residency over.
        return core::ptr::null_mut();
    }
    // SAFETY: `stream_backing` is non-null (checked above) and, per this fn's
    // contract, a live `SampleStream*` for the duration of this call.
    let asset = unsafe { deluge_sample_stream_asset_id(stream_backing) };
    let num_clusters = num_clusters_for(&geometry);
    // SAFETY: `handle` is non-null (checked above) and live for this source's
    // whole life -- forwarded from `deluge_streaming_resource_manager`'s own
    // contract (this fn's contract above).
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
        // Pool exhausted. Mirror `deluge_resource`'s OWN fixed-table-exhaustion
        // convention (e.g. `Manager::define_asset`'s `NONE` sentinel, and the
        // various `alloc_backing`/`request` null returns in manager.rs) rather
        // than the C++ side's diagnostic `FREEZE_WITH_ERROR("SSP1")`: this
        // crate is a plain `no_std` rlib with NO `#[panic_handler]` of its own
        // (same note as `deluge_resource::lib` -- the `deluge_rust` umbrella
        // provides the one for the whole dependency graph), and
        // `deluge_resource` itself never panics on a full fixed-capacity
        // table -- it always returns null/`NONE` and lets the caller decide.
        // The C++ FREEZE_WITH_ERROR is ALSO not a true halt on hardware (it
        // blocks then resumes -- see sample_source.cpp's own note) and still
        // returns `nullptr` afterward, so both sides converge on the same
        // caller-visible outcome (null), just reached differently.
        return core::ptr::null_mut();
    };
    // SAFETY: `slot` was just uniquely claimed via the masked check-and-set in
    // `claim_slot`, so no other caller can be concurrently reading/writing
    // `slot.source` -- this initializing write is exclusive.
    unsafe {
        *slot.source.get() = Some(source);
    }
    // Cache the manager handle for the source/lease-less `retain`/`release`
    // ABI (see the module doc): production runs exactly one boot-singleton
    // `deluge_resource` handle, so every `open()` stores the same value here
    // in practice.
    m_set(&ACTIVE_MANAGER.0, handle);
    slot.source.get() as *mut DelugeSampleSource
}

/// Make the region containing cluster `index` resident-or-scheduled, pin it,
/// and report which. See the header doc for the full contract.
///
/// # Safety
/// `src`, if non-null, must be a live pointer from `deluge_sample_source_open`
/// not yet closed. `out`, if non-null, must be a valid, writable
/// `DelugeSampleRegion` -- it is written only on `DELUGE_REGION_READY`.
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub unsafe extern "C" fn deluge_sample_region_acquire_ex(
    src: *mut DelugeSampleSource,
    index: u32,
    direction: i8,
    priority: u32,
    out: *mut DelugeSampleRegion,
) -> DelugeRegionState {
    // Null-tolerant, matching the header: open() can return null on pool
    // exhaustion, so `src` can legitimately be null here. There is no cursor
    // to schedule a fill on, so this is UNAVAILABLE -- `out` untouched.
    //
    // SAFETY: null-checked by `source_ref` itself; a non-null `src` is live
    // per this fn's own contract above.
    let Some(source) = (unsafe { source_ref(src as *const DelugeSampleSource) }) else {
        return DelugeRegionState::Unavailable;
    };
    let (state, region) = source.acquire_ex(index, direction, priority);
    if let (RegionState::Ready, Some(region)) = (state, region) {
        if !out.is_null() {
            // SAFETY: `out` is non-null and, per this fn's contract, a valid
            // writable `DelugeSampleRegion` on the caller's side.
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

/// Boolean form of [`deluge_sample_region_acquire_ex`]: `true` ==
/// `DELUGE_REGION_READY`.
///
/// # Safety
/// Same contract as [`deluge_sample_region_acquire_ex`].
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub unsafe extern "C" fn deluge_sample_region_acquire(
    src: *mut DelugeSampleSource,
    index: u32,
    direction: i8,
    priority: u32,
    out: *mut DelugeSampleRegion,
) -> bool {
    // SAFETY: forwards this fn's own contract, identical to acquire_ex's.
    (unsafe { deluge_sample_region_acquire_ex(src, index, direction, priority, out) })
        == DelugeRegionState::Ready
}

/// Residency of `index` as tracked by this cursor RIGHT NOW, without
/// acquiring anything. See the header doc for the full contract.
///
/// # Safety
/// `src`, if non-null, must be a live pointer from `deluge_sample_source_open`
/// not yet closed.
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub unsafe extern "C" fn deluge_sample_region_state(
    src: *const DelugeSampleSource,
    index: u32,
) -> DelugeRegionState {
    // SAFETY: forwarded from this fn's own contract above; null-checked by
    // `source_ref` itself.
    match unsafe { source_ref(src) } {
        Some(source) => source.state(index).into(),
        None => DelugeRegionState::Unavailable,
    }
}

/// Take an independent pin on the region's chunk, keyed on the opaque `lease`
/// token alone. `lease == 0` is a no-op, as is a call before any source has
/// ever been opened (nothing cached in [`ACTIVE_MANAGER`] to route through).
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub extern "C" fn deluge_sample_region_retain(lease: u64) {
    let handle = m_get(&ACTIVE_MANAGER.0);
    if handle.is_null() {
        return;
    }
    // SAFETY: `handle` was stored by a prior successful `open()` from a
    // caller-supplied live `deluge_resource` handle (the descriptor's own
    // contract, forwarded through `deluge_sample_source_open`); it stays live
    // for the process's remaining life per the boot-singleton contract
    // `ManagerResidency::new` documents.
    let res = unsafe { Resource::from_handle(handle) };
    res.retain_token(lease); // no-op on lease == 0 / a stale token (facade's own guard)
}

/// Drop an independent pin taken via [`deluge_sample_region_retain`]. See its
/// doc for the `ACTIVE_MANAGER` routing and the `lease == 0` / no-source
/// no-op cases.
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub extern "C" fn deluge_sample_region_release(lease: u64) {
    let handle = m_get(&ACTIVE_MANAGER.0);
    if handle.is_null() {
        return;
    }
    // SAFETY: see deluge_sample_region_retain.
    let res = unsafe { Resource::from_handle(handle) };
    res.release_token(lease);
}

/// Close the cursor, releasing any leases it still holds (current + pending +
/// prefetch), and return its pool slot for reuse. No-op on a null `src`.
///
/// # Safety
/// `src`, if non-null, must be a live pointer from `deluge_sample_source_open`
/// not yet closed, and must not be used again after this call.
#[cfg_attr(
    any(target_os = "none", feature = "host_app", feature = "sim"),
    unsafe(no_mangle)
)]
pub unsafe extern "C" fn deluge_sample_source_close(src: *mut DelugeSampleSource) {
    if src.is_null() {
        return;
    }
    // SAFETY: `src` was returned by `deluge_sample_source_open` as
    // `slot.source.get() as *mut DelugeSampleSource`; `Slot`'s `#[repr(C)]`
    // layout plus the `offset_of!` assertion above guarantee `source` sits at
    // offset 0, so reinterpreting the SAME address as `*const Slot` recovers
    // the enclosing slot -- mirrors `sample_source.cpp`'s
    // `reinterpret_cast<SampleSourceSlot*>(src)` (`release_source_slot`),
    // which relies on the identical `offsetof(...) == 0` invariant there.
    let slot = unsafe { &*(src as *const Slot) };
    // SAFETY: `slot` is a live pool slot (recovered above). This fn's own
    // contract requires `src` not be used again after this call, so nothing
    // else may be touching `slot.source` for the rest of this function --
    // exactly the same "pool slot integrity only" boundary `Slot`'s `Sync`
    // justification documents (tearing down a source while something else
    // still actively acquires through the SAME pointer is a pre-existing,
    // out-of-scope caller-discipline hazard, matching the C++ backing).
    let cell = unsafe { &mut *slot.source.get() };
    if let Some(source) = cell.as_ref() {
        // Drops current + pending + prefetch leases under `SampleSource`'s
        // own masked-cell discipline (see cursor.rs's `close`).
        source.close();
    }
    *cell = None; // reset the slot's payload to empty, ready for reuse
                  // Masked release: `Masked::enter` adapts to the caller's context --
                  // it masks the audio ISR when `close()` is called from main, and is a
                  // safe no-op when `close()` is itself on the render ISR (e.g. a reader
                  // reused via `ensureSource`) -- matching `claim_slot`'s own masked
                  // check-and-set (see `Slot`'s `Sync` doc for the full argument).
    m_set(&slot.in_use, false); // return the slot to the pool
}

#[cfg(test)]
mod tests {
    use super::*;
    extern crate std;

    use deluge_resource::value::COST_IO;
    use std::sync::Mutex;
    use std::vec::Vec;

    const CLUSTER_SIZE: usize = 16;

    /// Per-slot slab size: the real `StreamedChunk` front guard
    /// (`deluge_sample_fill::chunk::RUST_CHUNK_PAYLOAD_OFFSET`) plus this test's `CLUSTER_SIZE`
    /// payload, 16-aligned -- mirrors production's own `kChunkPayloadOffset`-sized `BACKING_SLAB`
    /// slots (`general_memory_allocator.cpp`) and `cursor.rs`'s own test harness.
    const BACKING_SIZE: usize =
        (deluge_sample_fill::chunk::RUST_CHUNK_PAYLOAD_OFFSET + CLUSTER_SIZE).next_multiple_of(16);

    /// Both tests below mutate the process-wide [`POOL`] and [`ACTIVE_MANAGER`]
    /// statics; under the default parallel test runner two tests running at once
    /// would race that shared state. Every test takes this lock for its whole run
    /// -- the standard process-wide-singleton test-lock pattern (same problem, same
    /// fix as the region-port harnesses use for their own shared statics).
    static TEST_LOCK: Mutex<()> = Mutex::new(());

    /// The real construct callback production registers for streamed-chunk assets
    /// (`chunk_residency.cpp`), reached through a thin `ConstructFn`-shaped forwarder. Same fixture
    /// as `cursor.rs`'s own `real_chunk_construct` (each test module keeps its own small copy
    /// rather than sharing one across `#[cfg(test)]` boundaries).
    ///
    /// # Safety
    /// `dest` must be a writable slab slot of at least `BACKING_SIZE` bytes -- this module's own
    /// slab satisfies that.
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

    /// Backing arena for the leaked test heap, kept alive for the process's
    /// remaining life -- mirrors `ManagerResidency`'s boot-singleton contract.
    struct TestHeap {
        _buf: Vec<u128>,
    }

    /// Build a manager + `BACKING_SIZE`-slotted slab over a fresh test heap and leak the arena --
    /// real streamed-chunk assets are `BACKING_SLAB` in production, not `BACKING_HEAP`.
    fn test_manager_handle() -> *mut DelugeResource {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf: Vec<u128> = std::vec![0u128; words];
        let ptr = buf.as_mut_ptr() as *mut u8;
        // SAFETY: `buf` is leaked below so the arena stays alive for the whole
        // test binary's life, matching `ManagerResidency::new`'s boot-singleton
        // contract.
        let h = unsafe { deluge_alloc::deluge_heap_create(ptr, words * 16) };
        std::mem::forget(TestHeap { _buf: buf });
        // SAFETY: `h` is the live heap handle just created above; 16 slots of
        // `BACKING_SIZE` bytes each is ample for these tests.
        let slab = unsafe { deluge_alloc::slab::deluge_slab_create_unmanaged(h, BACKING_SIZE, 16) };
        assert!(!slab.is_null());
        // SAFETY: `h` is the live heap handle just created above.
        let handle = unsafe { deluge_resource::deluge_resource_create(h, 4, 16) };
        assert!(!handle.is_null());
        // SAFETY: `handle`/`slab` are both live, over the same heap.
        unsafe { deluge_resource::deluge_resource_set_slab(handle, slab) };
        handle
    }

    /// Define a requestable, slab-backed asset whose `construct` is the real
    /// `deluge_streaming_chunk_construct` (no `materialize` -- matching production; this cursor
    /// drives readiness through `mark_index_ready`, standing in for the loader).
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

    /// Drive readiness through the manager directly (standing in for the loader this provider
    /// only schedules -- real fill is a later task), seeding the chunk's PAYLOAD region (not its
    /// slot base) with the per-index ramp `payload[b] = index as u8 + b as u8` the byte-check test
    /// below reads.
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

    /// The range guard counts FILE clusters, as `resident_bytes_for` and the region index do: a header that
    /// pushes the audio's tail past a cluster boundary adds one. A header of a quarter cluster before exactly two
    /// clusters of audio spans three, and the third holds the last quarter cluster -- a WAV's 44-byte header before
    /// 64 KiB of audio, at the device's 32 KiB clusters.
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

    /// Test doubles for `deluge_streaming_resource_manager`/
    /// `deluge_sample_stream_asset_id`: `sample_stream.cpp`'s/`async_fill.cpp`'s
    /// real C++ bodies aren't linked into this test binary, and there is no
    /// real `SampleStream` to build in a unit test, so `open()`'s bridge is
    /// exercised against these no-mangle overrides instead -- resolved by the
    /// linker in place of the `unsafe extern "C"` declarations above, exactly
    /// the same trick `lib.rs`'s `host_critical_section_stubs` already uses
    /// for `deluge_resource::sync`'s critical-section externs.
    ///
    /// Both tests that call `deluge_sample_source_open` set these via
    /// `set_test_bridge` before opening, under the same `TEST_LOCK` that
    /// already serializes this module's tests against the shared `POOL`/
    /// `ACTIVE_MANAGER` statics -- so a plain (non-atomic) `Cell` pair
    /// suffices here too.
    struct TestBridge {
        handle: Cell<*mut DelugeResource>,
        asset: Cell<u32>,
    }
    // SAFETY: every touch of both cells is serialized by TEST_LOCK (see the
    // doc above), so there is never concurrent access to race.
    unsafe impl Sync for TestBridge {}
    static TEST_BRIDGE: TestBridge = TestBridge {
        handle: Cell::new(core::ptr::null_mut()),
        asset: Cell::new(0),
    };

    fn set_test_bridge(handle: *mut DelugeResource, asset: u32) {
        TEST_BRIDGE.handle.set(handle);
        TEST_BRIDGE.asset.set(asset);
    }

    /// Test double for `deluge_streaming_resource_manager`. See `TestBridge`'s doc.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_streaming_resource_manager() -> *mut DelugeResource {
        TEST_BRIDGE.handle.get()
    }

    /// Test double for `deluge_sample_stream_asset_id`. Ignores `stream_backing`
    /// -- this test binary has no real `SampleStream` to dereference; see
    /// `dummy_stream_backing` and `TestBridge`'s doc.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_sample_stream_asset_id(_stream_backing: *mut DelugeStreamBacking) -> u32 {
        TEST_BRIDGE.asset.get()
    }

    /// A non-null dummy `stream_backing` for `deluge_sample_source_open`: the
    /// bridge test doubles above never dereference it (only `open()` itself
    /// null-checks it), so any non-null value stands in for the real
    /// `SampleStream*` production would pass.
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

        // A second open() -- alloc-free BY CONSTRUCTION (claim_slot only ever
        // flips an in_use Cell<bool> under a masked check-and-set, never
        // touches the heap) -- gets a DIFFERENT slot than the first, still-live one.
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

        // close() must return each slot to the pool: a handful of further
        // open/close cycles must keep succeeding (never null), proving slots
        // are reclaimed rather than leaked.
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

        // retain/release balance: forwarded through ACTIVE_MANAGER, no panic,
        // no crash, and idempotently reversible.
        deluge_sample_region_retain(out.lease);
        deluge_sample_region_release(out.lease);

        // SAFETY: `src` is live, not yet closed.
        unsafe { deluge_sample_source_close(src) };
    }

    /// Ports `sample_source_spec.cpp`'s "8b: acquire_ex's READY path is the
    /// boolean acquire's true, unchanged" (plus the boolean-specific half of
    /// "acquire on a not-yet-loaded cluster returns false and leaves `out`
    /// untouched"). [`deluge_sample_region_acquire`] -- the boolean C-ABI
    /// wrapper -- is exercised only here:
    /// every other test in this module calls `deluge_sample_region_acquire_ex`
    /// directly, so this test is the boolean wrapper's sole coverage.
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

        // A second cursor over the same already-ready index, so the boolean call
        // below is its own fresh acquire rather than re-acquiring what `via_ex`
        // already holds.
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
        // close() on a null src, and retain/release on a zero token, must be
        // silent no-ops (no panic, no crash).
        // SAFETY: `src` is null (the case under test).
        unsafe { deluge_sample_source_close(core::ptr::null_mut()) };
        deluge_sample_region_retain(0);
        deluge_sample_region_release(0);
    }
}
