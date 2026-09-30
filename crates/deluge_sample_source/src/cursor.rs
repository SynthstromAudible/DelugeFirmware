//! The per-reader region-port cursor: the tri-state `acquire_ex` state machine behind
//! `deluge_sample_region_*`.
//!
//! A cursor holds up to three pins:
//! - `current`: the loaded region the caller reads;
//! - `pending`: a `Loading` reservation from an earlier acquire, retained so its fill keeps
//!   progressing across defer/retry;
//! - `prefetch`: a reservation for the neighbouring region in the playback direction, so the next
//!   acquire of it is a resident hit.
//!
//! Slots hold RAII pins in `Cell<Option<R::Pin>>`; a transition drops the outgoing pin, releasing
//! exactly one lease, so lease balance follows from ownership.
//!
//! # Concurrency
//!
//! `acquire_ex` runs on the audio tier; `close` runs on the audio tier (a pooled reader reopened
//! for a new sample) or in thread mode (voice teardown). Every slot access, including the
//! `prefetch_index` sidecar, is therefore wrapped in `deluge_resource::sync::Masked`, the same
//! asymmetric critical section the manager's tables use: it masks everywhere except the audio
//! context, which is already atomic with respect to the others.
//!
//! # Invariant
//!
//! `current`, `pending` and `prefetch` never track the same index at once, so at most one slot
//! matches in [`SampleSource::state`]. Promotion (which consumes the prefetch for its index) and
//! the prefetch's own dedupe maintain it.

use crate::geometry::{resident_bytes_for, Geometry};
use crate::residency::{Get, RegionPin, Residency};
use core::cell::Cell;
use deluge_resource::sync::{m_get, Masked};

/// A region's residency, mirroring `DelugeRegionState`.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum RegionState {
    Ready = 1,
    Loading = 2,
    Unavailable = 3,
}

/// The region descriptor [`SampleSource::acquire_ex`] returns on [`RegionState::Ready`], mirroring
/// `DelugeSampleRegion`.
pub struct RegionOut {
    pub payload_base: *const u8,
    pub region_index: u32,
    pub resident_bytes: u32,
    pub lease: u64,
}

/// One region-port cursor; see the [module docs](crate::cursor) for its slots and invariant.
///
/// `prefetch_index` tracks the prefetch slot's index, `u32::MAX` when empty.
pub struct SampleSource<R: Residency> {
    residency: R,
    geo: Geometry,
    current: Cell<Option<R::Pin>>,
    pending: Cell<Option<R::Pin>>,
    prefetch: Cell<Option<R::Pin>>,
    prefetch_index: Cell<u32>,
}

impl<R: Residency> SampleSource<R> {
    /// Creates an empty cursor over `residency` for a sample with geometry `geo`.
    pub fn new(residency: R, geo: Geometry) -> Self {
        Self {
            residency,
            geo,
            current: Cell::new(None),
            pending: Cell::new(None),
            prefetch: Cell::new(None),
            prefetch_index: Cell::new(u32::MAX),
        }
    }

    /// Replaces a slot's pin under the mask, dropping the outgoing one inside the same window. The
    /// non-`Copy` counterpart of `deluge_resource::sync::m_set`.
    fn set_slot(slot: &Cell<Option<R::Pin>>, v: Option<R::Pin>) {
        let _m = Masked::enter();
        slot.set(v);
    }

    /// Drops any retained `Loading` reservation. Every outcome path calls this after its own pin is
    /// in hand, so a retry landing on the same chunk nets exactly one lease.
    fn release_pending(&self) {
        Self::set_slot(&self.pending, None); // drops the Option's pin -> releases its lease
    }

    /// The index `slot`'s pin tracks, if any.
    ///
    /// The take, inspect and restore share one masked window: split in two, a concurrent `close`
    /// could see the slot transiently empty, and the restore would revive the pin it released.
    fn slot_index(slot: &Cell<Option<R::Pin>>) -> Option<u32> {
        let _m = Masked::enter();
        let v = slot.take();
        let idx = v.as_ref().map(RegionPin::index);
        slot.set(v);
        idx
    }

    /// The index `current` is pinned to, if any.
    fn current_index(&self) -> Option<u32> {
        Self::slot_index(&self.current)
    }

    /// Masked read of the prefetch slot's index (`u32::MAX` when empty).
    fn prefetch_index(&self) -> u32 {
        m_get(&self.prefetch_index)
    }

    /// Replaces the prefetch pin and its index in one masked window, so no reader sees them
    /// disagree.
    fn set_prefetch(&self, pin: Option<R::Pin>, idx: u32) {
        let _m = Masked::enter();
        self.prefetch.set(pin); // drops the outgoing pin -> releases its lease
        self.prefetch_index.set(idx);
    }

    /// Takes the prefetch pin if it tracks `index`, transferring its lease to the caller in place
    /// of a fresh acquire. Returns `None`, leaving the prefetch untouched, on a miss.
    fn take_promoted(&self, index: u32) -> Option<R::Pin> {
        let _m = Masked::enter();
        if self.prefetch_index.get() != index {
            return None;
        }
        let pin = self.prefetch.take();
        if pin.is_some() {
            self.prefetch_index.set(u32::MAX);
        }
        pin
    }

    /// Prefetches the neighbour of `index` in `direction` (±1), if in range and not already the
    /// prefetch. Called on the `Ready` path only, after `current` is pinned. A `Loading` neighbour
    /// is kept as-is; `Unavailable` leaves the prefetch empty.
    fn prefetch_neighbour(&self, index: u32, direction: i8, priority: u32) {
        debug_assert!(
            direction == 1 || direction == -1,
            "prefetch direction must be ±1; direction 0 would self-prefetch and violate the never-same-index invariant"
        );
        let next_signed = index as i64 + direction as i64;
        if next_signed < 0 {
            return;
        }
        let next = next_signed as u32;
        if next >= self.residency.num_clusters() {
            return;
        }
        if self.prefetch_index() == next {
            return;
        }
        // Release the old prefetch before acquiring the new one.
        self.set_prefetch(None, u32::MAX);
        match self.residency.acquire(next, priority) {
            Get::Unavailable => {
                // The prefetch stays empty.
            }
            Get::Loading(pin) | Get::Ready(pin) => self.set_prefetch(Some(pin), next),
        }
    }

    /// Makes region `index` resident or scheduled, pins it, and reports which.
    ///
    /// Never blocks: a non-resident region is scheduled at loader `priority` and reported
    /// `Loading`. On `Ready` the region becomes `current` and the neighbour in `direction` (±1) is
    /// prefetched.
    pub fn acquire_ex(
        &self,
        index: u32,
        direction: i8,
        priority: u32,
    ) -> (RegionState, Option<RegionOut>) {
        // A prefetch hit is promoted in place of a fresh acquire and classified by live readiness;
        // one still loading moves to `pending`.
        let get = match self.take_promoted(index) {
            Some(pin) => {
                if pin.is_ready() {
                    Get::Ready(pin)
                } else {
                    Get::Loading(pin)
                }
            }
            None => self.residency.acquire(index, priority),
        };
        match get {
            Get::Unavailable => {
                // No fill is in flight, and any earlier pending reservation is moot.
                self.release_pending();
                (RegionState::Unavailable, None)
            }
            Get::Loading(pin) => {
                // Retain as pending; leave `current` alone, as the caller may still be reading it.
                self.release_pending();
                Self::set_slot(&self.pending, Some(pin));
                (RegionState::Loading, None)
            }
            Get::Ready(pin) => {
                // Any pending reservation is superseded; if it is this chunk (a retry that landed),
                // this drops the duplicate lease, leaving the one `current` is about to hold.
                self.release_pending();
                let token = pin.token();
                let payload_base = pin.payload();
                // Replace `current` unless it already tracks this index.
                let same = matches!(self.current_index(), Some(i) if i == index);
                if same {
                    // The acquire took a second lease on the chunk `current` already pins; drop it
                    // so re-acquiring is idempotent. Its payload and token equal `current`'s.
                    drop(pin);
                } else {
                    Self::set_slot(&self.current, Some(pin));
                }
                let out = RegionOut {
                    payload_base,
                    region_index: index,
                    resident_bytes: resident_bytes_for(index, &self.geo),
                    lease: token,
                };
                self.prefetch_neighbour(index, direction, priority);
                (RegionState::Ready, Some(out))
            }
        }
    }

    /// Whether `slot`'s pin tracks `index`, read in one masked window.
    fn slot_matches(slot: &Cell<Option<R::Pin>>, index: u32) -> bool {
        let _m = Masked::enter();
        let v = slot.take();
        let m = matches!(&v, Some(p) if p.index() == index);
        slot.set(v);
        m
    }

    /// The live state of `slot`'s pin if it tracks `index`, read in one masked window.
    fn slot_state_if_index(slot: &Cell<Option<R::Pin>>, index: u32) -> Option<RegionState> {
        let _m = Masked::enter();
        let v = slot.take();
        let result = match &v {
            Some(p) if p.index() == index => Some(if p.is_ready() {
                RegionState::Ready
            } else {
                RegionState::Loading
            }),
            _ => None,
        };
        slot.set(v);
        result
    }

    /// Returns the residency of `index` as this cursor tracks it, without acquiring anything.
    ///
    /// Checks `current`, then `pending`, then `prefetch`, with live readiness. Only a ready pin is
    /// ever made `current`, so a match there is always `Ready`.
    pub fn state(&self, index: u32) -> RegionState {
        if Self::slot_matches(&self.current, index) {
            return RegionState::Ready;
        }
        if let Some(s) = Self::slot_state_if_index(&self.pending, index) {
            return s;
        }
        if let Some(s) = Self::slot_state_if_index(&self.prefetch, index) {
            return s;
        }
        RegionState::Unavailable
    }

    /// Takes an independent pin by lease token. A no-op on `0` or a stale token; independent of
    /// this cursor's slots.
    pub fn retain(&self, token: u64) {
        self.residency.retain_token(token);
    }

    /// Drops an independent pin taken by [`Self::retain`].
    pub fn release(&self, token: u64) {
        self.residency.release_token(token);
    }

    /// Releases every pin the cursor holds.
    pub fn close(&self) {
        Self::set_slot(&self.current, None);
        Self::set_slot(&self.pending, None);
        self.set_prefetch(None, u32::MAX);
    }

    /// The index `pending` tracks, if any.
    #[cfg(test)]
    fn pending_index(&self) -> Option<u32> {
        Self::slot_index(&self.pending)
    }
}

// Lease-balance and state tests of the cursor over a real manager and real streamed-chunk backing.
#[cfg(test)]
mod tests {
    use super::*;
    use crate::geometry::Geometry;
    use crate::manager_residency::ManagerResidency;
    extern crate std;

    use core::ffi::c_void;
    use deluge_resource::value::COST_IO;
    use deluge_resource::DelugeResource;
    use std::vec::Vec;

    const CLUSTER_SIZE: usize = 16;
    const CHUNK_CAP: usize = 16;

    /// Per-slot slab size: the `StreamedChunk` header plus a `CLUSTER_SIZE` payload, 16-aligned,
    /// as production's slab slots are sized.
    const BACKING_SIZE: usize =
        (deluge_sample_fill::chunk::RUST_CHUNK_PAYLOAD_OFFSET + CLUSTER_SIZE).next_multiple_of(16);

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
        // SAFETY: `buf` is leaked below so the arena stays alive for the whole test
        // binary's life, matching `ManagerResidency::new`'s boot-singleton contract.
        let h = unsafe { deluge_alloc::deluge_heap_create(ptr, words * 16) };
        std::mem::forget(TestHeap { _buf: buf });
        // SAFETY: `h` is the live heap handle just created above.
        let slab =
            unsafe { deluge_alloc::slab::deluge_slab_create_unmanaged(h, BACKING_SIZE, CHUNK_CAP) };
        assert!(!slab.is_null());
        // SAFETY: `h` is the live heap handle just created above.
        let handle = unsafe { deluge_resource::deluge_resource_create(h, 4, CHUNK_CAP) };
        assert!(!handle.is_null());
        // SAFETY: `handle`/`slab` are both live, over the same heap.
        unsafe { deluge_resource::deluge_resource_set_slab(handle, slab) };
        handle
    }

    /// Defines a slab-backed streamed-chunk asset, with no `materialize`, as production does.
    fn define_ramp_asset(h: *mut DelugeResource) -> u32 {
        // SAFETY: `h` is a live handle (from `test_manager_handle`); the callback
        // has the required C-ABI signature.
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
    /// its payload with the ramp `payload[b] = index + b`. Releases the lease it takes, so lease
    /// counts are not skewed.
    fn mark_index_ready(h: *mut DelugeResource, asset: u32, index: u32) {
        // SAFETY: `h` is a live handle; these are the same FFI-safe C-ABI calls the
        // facade wraps.
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

    /// The sum of live lease counts across the manager's chunk table: the lease-balance oracle.
    fn total_leases(h: *mut DelugeResource) -> u64 {
        (0..CHUNK_CAP as u32)
            // SAFETY: `h` is a live handle; `lease_count_by_slot` tolerates any
            // slot index (returns 0 out of range), per its own doc.
            .map(|slot| unsafe {
                deluge_resource::deluge_resource_lease_count_by_slot(h, slot) as u64
            })
            .sum()
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

    /// A cursor over a fresh manager and asset, plus the handle and asset for driving readiness and
    /// reading lease counts.
    fn new_source(num_clusters: u32) -> (SampleSource<ManagerResidency>, *mut DelugeResource, u32) {
        let h = test_manager_handle();
        let asset = define_ramp_asset(h);
        let geo = test_geo(num_clusters);
        // SAFETY: `h` is a live handle for the test's duration (leaked arena).
        let residency = unsafe { ManagerResidency::new(h, asset, CLUSTER_SIZE, num_clusters) };
        (SampleSource::new(residency, geo), h, asset)
    }

    #[test]
    fn acquire_unavailable_leaves_nothing_leased() {
        let (src, h, _asset) = new_source(4);
        let (state, out) = src.acquire_ex(100, 0, 0); // out of range -> Unavailable
        assert_eq!(state, RegionState::Unavailable);
        assert!(out.is_none());
        assert_eq!(total_leases(h), 0, "unavailable must leak/hold nothing");
    }

    #[test]
    fn acquire_loading_retains_pending_and_leaves_current() {
        let (src, h, _asset) = new_source(4);
        let (state, out) = src.acquire_ex(0, 0, 0); // not resident -> reserved -> Loading
        assert_eq!(state, RegionState::Loading);
        assert!(out.is_none());
        assert_eq!(total_leases(h), 1, "pending holds exactly one lease");

        // Re-acquiring the still-loading index is idempotent: the fresh lease replaces the old
        // pending one.
        let (state2, out2) = src.acquire_ex(0, 0, 0);
        assert_eq!(state2, RegionState::Loading);
        assert!(out2.is_none());
        assert_eq!(
            total_leases(h),
            1,
            "re-acquiring a loading index must stay one pending lease"
        );
    }

    #[test]
    fn acquire_ready_pins_current_fills_out_and_fuses_old_current() {
        // direction=1, as real callers pass ±1: each Ready acquire also prefetches its forward
        // neighbour, and the lease counts below include that prefetch.
        let (src, h, asset) = new_source(4);
        mark_index_ready(h, asset, 0);

        let (state, out) = src.acquire_ex(0, 1, 0);
        assert_eq!(state, RegionState::Ready);
        let out0 = out.expect("ready must fill out");
        assert_eq!(out0.region_index, 0);
        assert_eq!(out0.resident_bytes, CLUSTER_SIZE as u32);
        // payload_base = the seeded ramp for index 0: dest[b] = 0 + b.
        for b in 0..CLUSTER_SIZE {
            // SAFETY: `payload_base` is cluster 0's live, leased backing of `CLUSTER_SIZE` bytes.
            let byte = unsafe { *out0.payload_base.add(b) };
            assert_eq!(byte, b as u8, "ramp byte {b} of cluster 0");
        }
        // current(0) plus the Loading prefetch of index 1.
        assert_eq!(
            total_leases(h),
            2,
            "one current lease + one standing prefetch lease after first Ready"
        );

        // Advance to index 1, promoting the prefetch: current(0) is released and a prefetch of
        // index 2 is added.
        mark_index_ready(h, asset, 1);
        let (state1, out1) = src.acquire_ex(1, 1, 0);
        assert_eq!(state1, RegionState::Ready);
        let out1 = out1.expect("ready must fill out");
        assert_eq!(out1.region_index, 1);
        assert_eq!(
            total_leases(h),
            2,
            "advancing current promotes+fuses net zero; prefetching index 2 adds one"
        );

        // Re-acquiring the current index is idempotent and reports the same lease and payload; the
        // prefetch of index 2 is untouched.
        let (state1b, out1b) = src.acquire_ex(1, 1, 0);
        assert_eq!(state1b, RegionState::Ready);
        let out1b = out1b.expect("ready must fill out");
        assert_eq!(
            total_leases(h),
            2,
            "re-acquiring the current index is net zero; the standing prefetch is untouched"
        );
        assert_eq!(
            out1b.lease, out1.lease,
            "dedupe: same token as standing current"
        );
        assert_eq!(
            out1b.payload_base, out1.payload_base,
            "dedupe: same payload as standing current"
        );
    }

    /// Acquiring the prefetched neighbour promotes it: the lease moves from `prefetch` to
    /// `current`.
    #[test]
    fn ready_prefetches_forward_neighbour_and_next_acquire_promotes_it() {
        // Two clusters, so index 1 has no neighbour to prefetch and the final count is 1.
        let (src, h, asset) = new_source(2);
        mark_index_ready(h, asset, 0);
        mark_index_ready(h, asset, 1);

        let (state0, _out0) = src.acquire_ex(0, 1, 0);
        assert_eq!(state0, RegionState::Ready);
        assert_eq!(
            total_leases(h),
            2,
            "current(0) + the prefetched neighbour(1)"
        );
        assert_eq!(
            src.state(1),
            RegionState::Ready,
            "neighbour 1 is prefetched and already ready"
        );

        // A non-promoting acquire would leave prefetch(1) standing beside current(1), keeping the
        // total at 2; promotion transfers the lease and releases current(0), so it drops to 1.
        let (state1, out1) = src.acquire_ex(1, 1, 0);
        assert_eq!(state1, RegionState::Ready);
        let out1 = out1.expect("ready must fill out");
        assert_eq!(out1.region_index, 1);
        assert_eq!(
            total_leases(h),
            1,
            "promotion transfers the prefetch lease into current -- no new reservation"
        );
        assert_eq!(
            src.state(0),
            RegionState::Unavailable,
            "old current(0) was fused away"
        );
        assert_eq!(src.state(1), RegionState::Ready, "current(1), promoted");
    }

    /// `state` resolves by each slot's own tracked index, not by assuming the prefetch is always
    /// one past the last acquire.
    #[test]
    fn indexed_state_is_truthful_after_jump_ahead_of_prefetch() {
        let (src, h, asset) = new_source(8);
        mark_index_ready(h, asset, 0);

        // acquire(0) -> Ready, prefetch(1) Loading.
        let (state0, _out0) = src.acquire_ex(0, 1, 0);
        assert_eq!(state0, RegionState::Ready);

        // acquire(5) -> Loading (pending=5). The Loading path does not prefetch, so nothing
        // tracks index 6.
        let (state5, out5) = src.acquire_ex(5, 1, 0);
        assert_eq!(state5, RegionState::Loading);
        assert!(out5.is_none());

        assert_eq!(src.state(0), RegionState::Ready, "current");
        assert_eq!(
            src.state(5),
            RegionState::Loading,
            "pending, the jumped-to index"
        );
        assert_eq!(
            src.state(1),
            RegionState::Loading,
            "the ORIGINAL prefetch neighbour, still standing and untouched by the jump"
        );
        assert_eq!(
            src.state(6),
            RegionState::Unavailable,
            "the TRUE neighbour of 5 -- nothing tracks it, since acquire(5) was LOADING \
             (step 5 never ran) and must NOT be conflated with index 5's own Loading state"
        );
    }

    /// `state` resolves `current`, then `pending`, then `prefetch`, reading readiness live.
    #[test]
    fn state_matches_current_pending_prefetch_by_tracked_index() {
        let (src, h, asset) = new_source(4);
        mark_index_ready(h, asset, 0);

        let (state0, _out0) = src.acquire_ex(0, 1, 0); // Ready(current=0), prefetch(1)=Loading
        assert_eq!(state0, RegionState::Ready);
        let (state2, _out2) = src.acquire_ex(2, 1, 0); // Loading(pending=2); prefetch(1) untouched
        assert_eq!(state2, RegionState::Loading);

        assert_eq!(src.state(0), RegionState::Ready, "current");
        assert_eq!(src.state(2), RegionState::Loading, "pending");
        assert_eq!(
            src.state(1),
            RegionState::Loading,
            "prefetch, not yet landed"
        );
        assert_eq!(src.state(3), RegionState::Unavailable, "tracked by no slot");

        // Land the prefetch's chunk without touching the cursor; `state` must see it live.
        mark_index_ready(h, asset, 1);
        assert_eq!(
            src.state(1),
            RegionState::Ready,
            "prefetch's readiness is a LIVE re-check, not cached at acquire time"
        );
    }

    /// `retain`/`release` move the lease balance by one each; `close` returns it to baseline.
    #[test]
    fn retain_release_balance_and_close_drops_all_slots() {
        let (src, h, asset) = new_source(2);
        assert_eq!(total_leases(h), 0, "baseline");
        mark_index_ready(h, asset, 0);

        let (state, out) = src.acquire_ex(0, 1, 0); // Ready(current=0), prefetch(1)=Loading
        assert_eq!(state, RegionState::Ready);
        let out = out.expect("ready must fill out");
        assert_eq!(total_leases(h), 2, "current + standing prefetch");

        src.retain(out.lease);
        assert_eq!(total_leases(h), 3, "retain adds an independent lease");
        src.release(out.lease);
        assert_eq!(total_leases(h), 2, "release drops it back");

        src.close();
        assert_eq!(
            total_leases(h),
            0,
            "close drops current+pending+prefetch back to baseline"
        );
    }

    /// Jumping to another index while a `Loading` reservation is outstanding supersedes it without
    /// leaking.
    #[test]
    fn jump_ahead_supersedes_pending_without_leak() {
        let (src, h, _asset) = new_source(8);

        let (state3, out3) = src.acquire_ex(3, 1, 0); // not resident -> Loading, pending=3
        assert_eq!(state3, RegionState::Loading);
        assert!(out3.is_none());
        assert_eq!(total_leases(h), 1, "one pending lease");

        let (state7, out7) = src.acquire_ex(7, 1, 0); // jump ahead -> Loading, pending=7
        assert_eq!(state7, RegionState::Loading);
        assert!(out7.is_none());
        assert_eq!(
            total_leases(h),
            1,
            "the old pending(3) is released, the new pending(7) leased -- net one"
        );
        assert_eq!(
            src.state(3),
            RegionState::Unavailable,
            "superseded, no longer tracked"
        );
        assert_eq!(src.state(7), RegionState::Loading, "the new pending");
    }

    /// The never-same-index invariant holds across a prefetch, a jump past it, a promotion to
    /// `Ready`, and a promotion to `Loading` (which moves the reservation to `pending`).
    #[test]
    fn slots_never_track_the_same_index() {
        fn assert_distinct(src: &SampleSource<ManagerResidency>) {
            let cur = src.current_index();
            let pending = src.pending_index();
            let pre = src.prefetch_index();
            let pre = (pre != u32::MAX).then_some(pre);
            let tracked: Vec<u32> = [cur, pending, pre].into_iter().flatten().collect();
            let mut sorted = tracked.clone();
            sorted.sort_unstable();
            sorted.dedup();
            assert_eq!(
                sorted.len(),
                tracked.len(),
                "slots must track pairwise-distinct indices: current={cur:?} pending={pending:?} prefetch={pre:?}"
            );
        }

        let (src, h, asset) = new_source(8);
        mark_index_ready(h, asset, 0);

        src.acquire_ex(0, 1, 0); // Ready(current=0), prefetch(1)=Loading
        assert_distinct(&src);

        src.acquire_ex(5, 1, 0); // jump ahead -> Loading(pending=5); prefetch(1) untouched
        assert_distinct(&src);

        mark_index_ready(h, asset, 1);
        src.acquire_ex(1, 1, 0); // promote(1) -> Ready: fuses pending(5) away, fuses current(0),
        assert_distinct(&src); // prefetches(2)=Loading

        src.acquire_ex(2, 1, 0); // promote(2), not ready -> Loading: moves into pending, empties prefetch
        assert_distinct(&src);

        mark_index_ready(h, asset, 2);
        src.acquire_ex(2, 1, 0); // fresh acquire (prefetch is empty) -> Ready: fuses pending(2) away,
        assert_distinct(&src); // prefetches(3)=Loading
    }

    /// A `Loading` region's lease is retained across retries, and when the fill lands the retry
    /// folds it into `current`: one lease, no duplicate.
    #[test]
    fn loading_retry_that_lands_ready_folds_into_a_single_lease() {
        let (src, h, asset) = new_source(4);

        // Not yet resident: reserved -> Loading, the lease retained across the call.
        let (state, out) = src.acquire_ex(1, 1, 0);
        assert_eq!(state, RegionState::Loading);
        assert!(out.is_none());
        assert_eq!(total_leases(h), 1, "pending holds exactly one lease");

        // Retrying before the fill lands is idempotent.
        let (state2, out2) = src.acquire_ex(1, 1, 0);
        assert_eq!(state2, RegionState::Loading);
        assert!(out2.is_none());
        assert_eq!(
            total_leases(h),
            1,
            "retrying before landing must stay one lease"
        );

        // The fill lands: the retry reports Ready and folds the pending lease into `current`. The
        // neighbour prefetch adds the only other lease.
        mark_index_ready(h, asset, 1);
        let (state3, out3) = src.acquire_ex(1, 1, 0);
        assert_eq!(state3, RegionState::Ready);
        let out3 = out3.expect("ready must fill out");
        assert_eq!(out3.region_index, 1);
        assert_eq!(
            total_leases(h),
            2,
            "current(1), folded from the retained pending, plus the fresh prefetch(2)"
        );
    }

    /// An `Unavailable` acquire releases an outstanding pending lease rather than stranding it,
    /// and a zero token is a no-op for `retain`/`release`.
    ///
    /// A leased index cannot be evicted, so an out-of-range index stands in for a region that
    /// becomes unreservable.
    #[test]
    fn pending_lease_is_dropped_not_stranded_when_the_next_acquire_is_unavailable() {
        let (src, h, _asset) = new_source(4);

        let (state, out) = src.acquire_ex(1, 1, 0); // not resident -> Loading, pending=1
        assert_eq!(state, RegionState::Loading);
        assert!(out.is_none());
        assert_eq!(total_leases(h), 1, "pending holds exactly one lease");

        let (state2, out2) = src.acquire_ex(100, 1, 0); // out of range -> Unavailable
        assert_eq!(state2, RegionState::Unavailable);
        assert!(out2.is_none());
        assert_eq!(
            total_leases(h),
            0,
            "the outstanding pending lease is released, not stranded"
        );

        // lease == 0 is a no-op in both directions and does not disturb the balance.
        src.retain(0);
        src.release(0);
        assert_eq!(total_leases(h), 0);
    }

    /// Promoting a prefetch that is still loading moves it to `pending`, and `state` keeps
    /// reporting it `Loading`, not `Unavailable`.
    #[test]
    fn promoted_prefetch_that_is_not_yet_ready_lands_in_pending_and_state_stays_truthful() {
        let (src, h, asset) = new_source(2);
        mark_index_ready(h, asset, 0);

        // acquire(0) -> Ready, prefetch(1) Loading.
        let (state0, _out0) = src.acquire_ex(0, 1, 0);
        assert_eq!(state0, RegionState::Ready);
        assert_eq!(
            src.state(1),
            RegionState::Loading,
            "standing prefetch, not yet landed"
        );

        // Acquire the prefetched index while still loading: it moves into `pending`.
        let (state1, out1) = src.acquire_ex(1, 1, 0);
        assert_eq!(state1, RegionState::Loading);
        assert!(out1.is_none());

        // `Unavailable` here would tell a deferring caller nothing is in flight when a fill is.
        assert_eq!(src.state(1), RegionState::Loading);
        assert_eq!(total_leases(h), 2, "current(0) + the promoted pending(1)");

        // It lands: `state` reports Ready without acquiring.
        mark_index_ready(h, asset, 1);
        assert_eq!(src.state(1), RegionState::Ready);

        // The retry folds the pending lease into `current`. Two clusters, so no neighbour
        // prefetch: exactly one lease.
        let (state1b, out1b) = src.acquire_ex(1, 1, 0);
        assert_eq!(state1b, RegionState::Ready);
        let out1b = out1b.expect("ready must fill out");
        assert_eq!(out1b.region_index, 1);
        assert_eq!(
            total_leases(h),
            1,
            "current(1) only -- old current(0) fused, pending folded, nothing further to prefetch"
        );
    }

    /// SplitMix64, for deterministic op sequences.
    fn splitmix64(state: &mut u64) -> u64 {
        *state = state.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = *state;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    /// Seeded random walks of cursor operations (steps, jumps, state queries, fills, retains)
    /// check lease balance.
    ///
    /// - After every step, outstanding leases are at most three cursor-owned ones plus the
    ///   test's explicit retains, so a leak is caught on the step that causes it.
    /// - After releasing the retains and closing, the total returns to zero.
    #[test]
    fn random_walk_lease_balance_is_net_zero_across_many_seeds() {
        use std::collections::HashMap;
        use std::vec::Vec;

        const OPS_PER_SEED: u32 = 150;
        const MAX_CURSOR_OWNED_LEASES: u64 = 3; // current + pending + prefetch, at most one lease each

        for seed in 0..256u64 {
            let mut rng = seed ^ 0xD1B5_4A32_D192_ED03;

            // A fresh manager per seed keeps seeds independent.
            let (mut src, h, asset) = new_source(6);
            let num_clusters = 6u32;

            let mut pos: u32 = 0;
            let mut known_tokens: Vec<u64> = Vec::new();
            let mut retained: HashMap<u64, u32> = HashMap::new();

            let outstanding_retains =
                |m: &HashMap<u64, u32>| -> u64 { m.values().map(|&c| u64::from(c)).sum() };

            for _ in 0..OPS_PER_SEED {
                let r = splitmix64(&mut rng);
                match r % 7 {
                    0 => {
                        // Forward playback step.
                        pos = (pos + 1) % num_clusters;
                        let (_state, out) = src.acquire_ex(pos, 1, 0);
                        if let Some(out) = out {
                            known_tokens.push(out.lease);
                        }
                    }
                    1 => {
                        // Backward playback step.
                        pos = (pos + num_clusters - 1) % num_clusters;
                        let (_state, out) = src.acquire_ex(pos, -1, 0);
                        if let Some(out) = out {
                            known_tokens.push(out.lease);
                        }
                    }
                    2 => {
                        // Jump to an arbitrary in-range index, either direction.
                        let idx = (splitmix64(&mut rng) % u64::from(num_clusters)) as u32;
                        let dir: i8 = if splitmix64(&mut rng) & 1 == 0 { 1 } else { -1 };
                        pos = idx;
                        let (_state, out) = src.acquire_ex(idx, dir, 0);
                        if let Some(out) = out {
                            known_tokens.push(out.lease);
                        }
                    }
                    3 => {
                        // State query: must never acquire or mutate.
                        let idx = (splitmix64(&mut rng) % u64::from(num_clusters)) as u32;
                        let _ = src.state(idx);
                    }
                    4 => {
                        // Land a fill, standing in for the loader.
                        let idx = (splitmix64(&mut rng) % u64::from(num_clusters)) as u32;
                        mark_index_ready(h, asset, idx);
                    }
                    5 => {
                        // Retain a previously seen token; a stale one is a no-op.
                        if !known_tokens.is_empty() {
                            let i = (splitmix64(&mut rng) as usize) % known_tokens.len();
                            let token = known_tokens[i];
                            src.retain(token);
                            *retained.entry(token).or_insert(0) += 1;
                        }
                    }
                    6 => {
                        // Release one outstanding explicit retain, if any.
                        let live: Vec<u64> = retained
                            .iter()
                            .filter(|&(_, &c)| c > 0)
                            .map(|(&t, _)| t)
                            .collect();
                        if !live.is_empty() {
                            let i = (splitmix64(&mut rng) as usize) % live.len();
                            let token = live[i];
                            src.release(token);
                            *retained.get_mut(&token).expect("just selected as live") -= 1;
                        }
                    }
                    _ => unreachable!("r % 7 is in 0..7"),
                }

                // Per-step bound: the cursor's slot leases plus this test's retains.
                let budget = MAX_CURSOR_OWNED_LEASES + outstanding_retains(&retained);
                assert!(
                    total_leases(h) <= budget,
                    "seed {seed}: total_leases({}) exceeds the max-possible budget ({budget}) -- \
                     cursor slots hold at most {MAX_CURSOR_OWNED_LEASES} leases, so this can only \
                     be a leak or a double-lease",
                    total_leases(h)
                );

                // Occasionally reopen over the same manager: `close` drops exactly the cursor's own
                // slots; explicit retains survive it.
                if r.is_multiple_of(37) {
                    src.close();
                    known_tokens.clear();
                    // SAFETY: `h` is the live handle `new_source` created.
                    let residency =
                        unsafe { ManagerResidency::new(h, asset, CLUSTER_SIZE, num_clusters) };
                    src = SampleSource::new(residency, test_geo(num_clusters));
                    pos = 0;
                }
            }

            // Release every explicit retain, then close: the total must return to zero.
            for (token, count) in retained.drain() {
                for _ in 0..count {
                    src.release(token);
                }
            }
            src.close();
            assert_eq!(
                total_leases(h),
                0,
                "seed {seed}: leaked leases after full release+close"
            );
        }
    }
}
