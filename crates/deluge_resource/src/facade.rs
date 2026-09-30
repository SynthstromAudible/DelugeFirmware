//! Safe Rust interface to the resource manager, over the same [`Manager`] methods as the C ABI.
//!
//! Every operation that takes a lease returns a [`Lease`] guard, never a bare [`Chunk`], so the
//! lease is released exactly once on drop. [`Resource::from_handle`] is the only `unsafe` entry.

use crate::manager::{mgr, Manager};
use crate::DelugeResource;
use core::ptr::NonNull;

/// A chunk's backing pointer, which is its identity in the manager.
///
/// Not a lease: it holds nothing and releases nothing. It has no lifetime because the manager's
/// tables are never freed; a stale `Chunk` is caught by the manager's re-validation, so operations
/// on one are no-ops or misses.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Chunk(NonNull<u8>);

impl Chunk {
    /// Maps a manager-returned pointer to a handle; null (a miss) becomes `None`.
    fn from_ptr(p: *mut u8) -> Option<Self> {
        NonNull::new(p).map(Chunk)
    }

    /// Returns the raw backing pointer.
    pub fn as_ptr(self) -> *mut u8 {
        self.0.as_ptr()
    }
}

/// A borrowed view of a manager handle; not an owner.
pub struct Resource<'m> {
    mgr: &'m Manager,
}

impl<'m> Resource<'m> {
    /// Wraps a handle from `deluge_resource_create` or `deluge_resource_create_unhooked`.
    ///
    /// # Safety
    ///
    /// `h` must be a non-null live handle, valid for at least `'m`.
    pub unsafe fn from_handle(h: *mut DelugeResource) -> Self {
        // SAFETY: forwarded to the caller's contract.
        Resource {
            mgr: unsafe { mgr(h) },
        }
    }

    /// Leases chunk `index` of `asset` if resident and ready. Returns a bare `Chunk` holding a
    /// lease, so it stays private; [`Resource::acquire_leased`] wraps it.
    fn try_acquire(&self, asset: u32, index: u32) -> Option<Chunk> {
        Chunk::from_ptr(self.mgr.try_acquire(asset, index))
    }

    /// Returns chunk `index` of `asset` if resident, ready or not.
    ///
    /// Takes no lease and leaves recency alone, so it does not perturb eviction order. Use
    /// [`Resource::is_ready`] to check for loaded data.
    pub fn peek(&self, asset: u32, index: u32) -> Option<Chunk> {
        Chunk::from_ptr(self.mgr.peek(asset, index))
    }

    /// Leases chunk `index` of `asset`, constructing it without I/O if not resident, so a loader
    /// can fill it and call [`Resource::mark_ready`].
    ///
    /// `None` on OOM, a full table with nothing evictable, or an asset without a `construct`
    /// callback. Dropping the `Lease` releases the reservation.
    pub fn request(&self, asset: u32, index: u32, size: usize) -> Option<Lease> {
        Chunk::from_ptr(self.mgr.request(asset, index, size))
            .map(|c| Lease::adopt(self.mgr as *const Manager, c))
    }

    /// Marks a `request`ed chunk as loaded. No-op if it is no longer resident.
    pub fn mark_ready(&self, chunk: Chunk) {
        self.mgr.mark_ready(chunk.as_ptr());
    }

    /// Returns the slot index backing `chunk`, as the loader queue and lease counts key by slot;
    /// `DELUGE_RESOURCE_NO_SLOT` if not resident.
    pub fn slot_of(&self, chunk: Chunk) -> u32 {
        self.mgr.slot_of(chunk.as_ptr())
    }

    /// Returns the `(asset, index)` of `chunk`, or `None` if not resident. `asset` is `u32::MAX`
    /// for an adopted chunk.
    pub fn chunk_ident(&self, chunk: Chunk) -> Option<(u32, u32)> {
        self.mgr.chunk_ident(chunk.as_ptr())
    }

    /// Enqueues the chunk at `slot` at `priority` (lower = more urgent); a re-enqueue updates the
    /// priority.
    pub fn loader_enqueue(&self, slot: u32, priority: u32) {
        self.mgr.loader_enqueue(slot, priority);
    }

    /// Removes the chunk at `slot` from the load queue, cancelling its pending load and dropping
    /// any queue-owned lease.
    pub fn loader_remove(&self, slot: u32) {
        self.mgr.loader_remove(slot);
    }

    /// Pops the most urgent queued, still-leased chunk, or `None` if there is none.
    ///
    /// Takes no lease: the chunk's existing leases belong to its requester (or the queue), so the
    /// loader must not wrap it in a `Lease` of its own, which would release one lease too many.
    pub fn loader_next(&self) -> Option<Chunk> {
        Chunk::from_ptr(self.mgr.loader_next())
    }

    /// Returns the hard-lease count of the chunk at `slot`; 0 if out of range or free.
    pub fn lease_count_by_slot(&self, slot: u32) -> u32 {
        self.mgr.lease_count_by_slot(slot)
    }

    /// Takes a hard lease on `chunk` and returns its guard.
    pub fn lease(&self, chunk: Chunk) -> Lease {
        self.mgr.add_lease(chunk.as_ptr());
        // The manager is never freed, so the pointer outlives `'m` (see `Lease`).
        Lease::adopt(self.mgr as *const Manager, chunk)
    }

    /// Leases chunk `index` of `asset` if it is resident and ready; `None` otherwise. RT-safe.
    pub fn acquire_leased(&self, asset: u32, index: u32) -> Option<Lease> {
        // `try_acquire` already leased; wrap that lease rather than taking a second.
        self.try_acquire(asset, index)
            .map(|c| Lease::adopt(self.mgr as *const Manager, c))
    }

    /// Returns an opaque pin token for `chunk` (its slot and generation), or 0 if not resident.
    ///
    /// Pass it to [`Resource::retain_token`] and [`Resource::release_token`]; a token that
    /// outlives its chunk makes both no-ops.
    pub fn pin_token(&self, chunk: Chunk) -> u64 {
        let slot = self.mgr.slot_of(chunk.as_ptr());
        if slot == crate::manager::NO_SLOT {
            return 0;
        }
        let gen = self.mgr.generation_of_slot(slot);
        if gen == 0 {
            return 0;
        }
        ((slot as u64) << 32) | (gen as u64)
    }

    /// Takes a hard lease through `token`, for owners across the C ABI that cannot hold a
    /// [`Lease`]. No-op on 0 or a stale token.
    pub fn retain_token(&self, token: u64) {
        if token == 0 {
            return;
        }
        let slot = (token >> 32) as u32;
        let gen = token as u32;
        self.mgr.retain_by_slot_gen(slot, gen);
    }

    /// Drops a lease taken with [`Resource::retain_token`]. No-op on 0 or a stale token.
    pub fn release_token(&self, token: u64) {
        if token == 0 {
            return;
        }
        let slot = (token >> 32) as u32;
        let gen = token as u32;
        self.mgr.release_by_slot_gen(slot, gen);
    }

    /// Returns whether `chunk` is resident and loaded.
    pub fn is_ready(&self, chunk: Chunk) -> bool {
        self.mgr.is_ready_by_ptr(chunk.as_ptr())
    }
}

/// A hard lease on a resident chunk, released exactly once on drop. Neither `Copy` nor `Clone`.
///
/// Holds the manager as a raw pointer rather than `&'m Manager`, so it has no lifetime and can be
/// stored long-term (e.g. in a `Cell<Option<Lease>>`). This is sound because a manager is never
/// freed or moved once created.
pub struct Lease {
    mgr: *const Manager,
    chunk: Chunk,
}

impl Lease {
    /// Wraps a lease the caller has already taken on `chunk`; the guard releases exactly that one.
    fn adopt(mgr: *const Manager, chunk: Chunk) -> Self {
        debug_assert!(!mgr.is_null(), "Lease over a null manager pointer");
        Lease { mgr, chunk }
    }

    /// Returns the leased chunk.
    pub fn chunk(&self) -> Chunk {
        self.chunk
    }
}

impl Drop for Lease {
    fn drop(&mut self) {
        // SAFETY: `self.mgr` came from a live `&Manager`, and a manager is never freed. `release`
        // masks as needed, so this drop is safe from any context.
        let mgr = unsafe { &*self.mgr };
        mgr.release(self.chunk.as_ptr());
    }
}

#[cfg(test)]
mod tests {
    extern crate std;

    use super::*;
    use crate::manager::BACKING_HEAP;
    use crate::value::COST_IO;
    use core::ffi::c_void;
    use std::vec::Vec;

    unsafe extern "C" fn mock_materialize(
        _ctx: *mut c_void,
        _owner: *mut c_void,
        _index: u32,
        dest: *mut u8,
        len: usize,
    ) -> bool {
        // SAFETY: `dest`/`len` come from the manager's just-allocated backing.
        unsafe { core::ptr::write_bytes(dest, 0xAB, len) };
        true
    }

    unsafe extern "C" fn mock_construct(
        _ctx: *mut c_void,
        _owner: *mut c_void,
        _index: u32,
        dest: *mut u8,
    ) {
        // SAFETY: `dest` comes from the manager's just-allocated backing.
        unsafe { *dest = 0xC0 };
    }

    const CHUNK_SIZE: usize = 4096;

    /// A manager over a 16-aligned test arena, kept alive alongside the handle.
    struct TestResource {
        _buf: Vec<u128>,
        handle: *mut DelugeResource,
    }

    fn test_resource() -> TestResource {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf = std::vec![0u128; words];
        // SAFETY: `buf` is a live 16-aligned arena of `words * 16` bytes, kept in `_buf`.
        let h =
            unsafe { deluge_alloc::deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16) };
        // SAFETY: `h` is the live heap handle just created above.
        let handle = unsafe { crate::deluge_resource_create(h, 4, 16) };
        assert!(!handle.is_null());
        TestResource { _buf: buf, handle }
    }

    impl TestResource {
        fn resource(&self) -> Resource<'_> {
            // SAFETY: `self.handle` is live as long as `self` is.
            unsafe { Resource::from_handle(self.handle) }
        }

        /// Defines a requestable test asset.
        fn define_test_asset(&self) -> u32 {
            // SAFETY: `self.handle` is live.
            let asset = unsafe {
                crate::deluge_resource_define_asset(
                    self.handle,
                    core::ptr::null_mut(),
                    Some(mock_materialize),
                    None,
                    core::ptr::null_mut(),
                    COST_IO,
                    BACKING_HEAP,
                )
            };
            unsafe {
                crate::deluge_resource_set_construct(self.handle, asset, Some(mock_construct));
            }
            asset
        }

        fn acquire_leased(&self, asset: u32, index: u32) -> Option<Lease> {
            self.resource().acquire_leased(asset, index)
        }
        fn request(&self, asset: u32, index: u32, size: usize) -> Option<Lease> {
            self.resource().request(asset, index, size)
        }
        fn mark_ready(&self, chunk: Chunk) {
            self.resource().mark_ready(chunk)
        }
        fn slot_of(&self, chunk: Chunk) -> u32 {
            self.resource().slot_of(chunk)
        }
        fn chunk_ident(&self, chunk: Chunk) -> Option<(u32, u32)> {
            self.resource().chunk_ident(chunk)
        }
        fn lease_count_by_slot(&self, slot: u32) -> u32 {
            self.resource().lease_count_by_slot(slot)
        }
        fn lease(&self, chunk: Chunk) -> Lease {
            self.resource().lease(chunk)
        }
        fn pin_token(&self, chunk: Chunk) -> u64 {
            self.resource().pin_token(chunk)
        }
        fn retain_token(&self, token: u64) {
            self.resource().retain_token(token)
        }
        fn release_token(&self, token: u64) {
            self.resource().release_token(token)
        }
    }

    #[test]
    fn try_acquire_reports_resident_ready_and_none_on_miss() {
        let rsrc = test_resource();
        let asset = rsrc.define_test_asset();
        assert!(rsrc.acquire_leased(asset, 0).is_none()); // not resident yet
        let req = rsrc.request(asset, 0, CHUNK_SIZE).expect("request");
        let c = req.chunk();
        rsrc.mark_ready(c);
        let got = rsrc.acquire_leased(asset, 0).expect("now ready");
        assert_eq!(got.chunk(), c); // `req` and `got` each hold, and release, their own lease
    }

    #[test]
    fn lease_drop_releases_exactly_once() {
        let rsrc = test_resource();
        let asset = rsrc.define_test_asset();
        let req = rsrc.request(asset, 0, CHUNK_SIZE).unwrap();
        let c = req.chunk();
        rsrc.mark_ready(c);
        let slot = rsrc.slot_of(c);
        let base = rsrc.lease_count_by_slot(slot); // pre-existing lease count (incl. `req`'s)
        {
            let _l = rsrc.lease(c);
            assert_eq!(rsrc.lease_count_by_slot(slot), base + 1); // took one
        } // _l drops here
        assert_eq!(rsrc.lease_count_by_slot(slot), base); // released exactly one
    }

    #[test]
    fn acquire_leased_takes_exactly_one_lease_and_drop_releases_it() {
        // `try_acquire` already leases on a hit; a second `lease()` would leak one.
        let rsrc = test_resource();
        let asset = rsrc.define_test_asset();
        let req = rsrc.request(asset, 0, CHUNK_SIZE).unwrap();
        let c = req.chunk();
        rsrc.mark_ready(c);
        let slot = rsrc.slot_of(c);
        let base = rsrc.lease_count_by_slot(slot);
        {
            let l = rsrc
                .resource()
                .acquire_leased(asset, 0)
                .expect("resident+ready");
            assert_eq!(l.chunk(), c);
            assert_eq!(rsrc.lease_count_by_slot(slot), base + 1);
        }
        assert_eq!(rsrc.lease_count_by_slot(slot), base);
    }

    /// `peek` hits whether or not the chunk is ready, and takes no lease and leaves recency alone,
    /// so it does not change which chunk is evicted.
    #[test]
    fn peek_is_resident_not_ready_gated_and_perturbs_neither_leases_nor_eviction_order() {
        let rsrc = test_resource();
        let asset = rsrc.define_test_asset();

        // Not resident -> None.
        assert!(rsrc.resource().peek(asset, 0).is_none());

        // Resident but not ready (`request` leaves `ready = false` until `mark_ready`).
        let req0 = rsrc.request(asset, 0, CHUNK_SIZE).unwrap();
        let c0 = req0.chunk();
        let slot0 = rsrc.slot_of(c0);
        let base0 = rsrc.lease_count_by_slot(slot0);
        let peeked = rsrc
            .resource()
            .peek(asset, 0)
            .expect("resident-but-not-ready is still a peek hit");
        assert_eq!(peeked, c0);
        assert_eq!(
            rsrc.lease_count_by_slot(slot0),
            base0,
            "peek must not take a lease"
        );

        // Now ready: same chunk, still no lease taken by peek.
        rsrc.mark_ready(c0);
        let peeked_ready = rsrc.resource().peek(asset, 0).expect("resident and ready");
        assert_eq!(peeked_ready, c0);
        assert_eq!(rsrc.lease_count_by_slot(slot0), base0);

        // Release req0's lease so c0 becomes evictable, then give it a strictly
        // newer (higher-recency) sibling c1.
        drop(req0);
        let req1 = rsrc.request(asset, 1, CHUNK_SIZE).unwrap();
        let c1 = req1.chunk();
        rsrc.mark_ready(c1);
        drop(req1);

        // c0 is older, so it is the victim unless peeking bumped its recency.
        for _ in 0..5 {
            assert_eq!(rsrc.resource().peek(asset, 0), Some(c0));
        }
        // SAFETY: `rsrc.handle` is the live handle backing this test's manager.
        let evicted = unsafe { crate::deluge_resource_try_evict(rsrc.handle) };
        assert!(evicted, "something evictable should have been reclaimed");
        assert!(
            rsrc.resource().peek(asset, 0).is_none(),
            "peeking c0 repeatedly must not have bumped its recency past c1's"
        );
        assert!(
            rsrc.resource().peek(asset, 1).is_some(),
            "c1 (never peeked) must still be resident"
        );
    }

    /// `Lease` has no lifetime, so it can live in a `Cell<Option<Lease>>`; dropping the taken
    /// value releases the lease.
    #[test]
    fn lease_is_storable_in_cell_option_and_take_drops_it() {
        use core::cell::Cell;

        let rsrc = test_resource();
        let asset = rsrc.define_test_asset();
        let req = rsrc.request(asset, 0, CHUNK_SIZE).unwrap();
        let c = req.chunk();
        rsrc.mark_ready(c);
        let slot = rsrc.slot_of(c);
        let base = rsrc.lease_count_by_slot(slot);

        let cell: Cell<Option<Lease>> = Cell::new(Some(rsrc.lease(c)));
        assert_eq!(rsrc.lease_count_by_slot(slot), base + 1);

        let taken = cell.take();
        assert!(cell.take().is_none()); // cell is empty after the take
        assert!(taken.is_some());
        assert_eq!(rsrc.lease_count_by_slot(slot), base + 1); // still held by `taken`

        drop(taken); // releases
        assert_eq!(rsrc.lease_count_by_slot(slot), base);
    }

    /// A `Lease` dropped from thread mode masks, one dropped from the audio context does not, and
    /// each decrements exactly once.
    #[test]
    fn lease_drop_is_masked_from_main_and_lockfree_from_isr() {
        use crate::sync::stubs;

        let rsrc = test_resource();
        let asset = rsrc.define_test_asset();
        let req = rsrc.request(asset, 0, CHUNK_SIZE).unwrap();
        let c = req.chunk();
        rsrc.mark_ready(c);
        let slot = rsrc.slot_of(c);
        let base = rsrc.lease_count_by_slot(slot); // includes `req`'s own lease

        // Thread mode: the drop masks.
        stubs::audio_isr_context_set(false);
        let l = rsrc.lease(c);
        assert_eq!(rsrc.lease_count_by_slot(slot), base + 1, "lease took one");
        stubs::cs_reset_counts();
        drop(l); // Lease::drop -> Manager::release, masked
        let entered = stubs::cs_enter_count();
        assert!(
            entered >= 1,
            "main-context Lease drop must enter the critical section on release"
        );
        assert_eq!(
            stubs::cs_exit_count(),
            entered,
            "every masked enter is balanced by an exit"
        );
        assert_eq!(
            rsrc.lease_count_by_slot(slot),
            base,
            "masked release decremented exactly one lease"
        );

        // Audio context: the drop does not mask.
        stubs::audio_isr_context_set(true);
        let l = rsrc.lease(c); // add_lease here is itself lock-free (ISR context)
        stubs::cs_reset_counts();
        drop(l); // Lease::drop -> Manager::release, lock-free on the ISR path
        assert_eq!(
            stubs::cs_enter_count(),
            0,
            "ISR-context Lease drop must not take the critical section"
        );
        assert_eq!(stubs::cs_exit_count(), 0);
        // Still in the audio context, so this read does not disturb the counts above.
        assert_eq!(
            rsrc.lease_count_by_slot(slot),
            base,
            "lock-free ISR release decremented exactly one lease"
        );

        stubs::audio_isr_context_set(false); // restore for other tests on this thread
    }

    /// Interleaved thread-mode and audio-context lease drops on different slots each release only
    /// their own lease.
    #[test]
    fn interleaved_main_and_isr_lease_drops_on_different_slots_never_cross_corrupt() {
        use crate::sync::stubs;

        let rsrc = test_resource();
        let asset = rsrc.define_test_asset();

        // Two distinct resident+ready chunks -> two distinct chunk-table slots.
        let req0 = rsrc.request(asset, 0, CHUNK_SIZE).unwrap();
        let c0 = req0.chunk();
        rsrc.mark_ready(c0);
        let req1 = rsrc.request(asset, 1, CHUNK_SIZE).unwrap();
        let c1 = req1.chunk();
        rsrc.mark_ready(c1);
        let s0 = rsrc.slot_of(c0);
        let s1 = rsrc.slot_of(c1);
        assert_ne!(s0, s1, "the two chunks must occupy distinct slots");
        let base0 = rsrc.lease_count_by_slot(s0);
        let base1 = rsrc.lease_count_by_slot(s1);

        // Take a main-context lease on slot0 and an ISR-context lease on slot1.
        stubs::audio_isr_context_set(false);
        let lm = rsrc.lease(c0); // main, slot0
        stubs::audio_isr_context_set(true);
        let li = rsrc.lease(c1); // ISR, slot1
        assert_eq!(rsrc.lease_count_by_slot(s0), base0 + 1);
        assert_eq!(rsrc.lease_count_by_slot(s1), base1 + 1);

        // Interleave the drops: ISR release (lock-free) first, then main release (masked).
        drop(li); // still in ISR context
        stubs::audio_isr_context_set(false);
        drop(lm); // main context

        // Each slot decremented exactly its own lease; no cross-slot leakage either way.
        assert_eq!(
            rsrc.lease_count_by_slot(s0),
            base0,
            "main-path release on slot0 decremented only slot0"
        );
        assert_eq!(
            rsrc.lease_count_by_slot(s1),
            base1,
            "ISR-path release on slot1 decremented only slot1"
        );
    }

    #[test]
    fn chunk_ident_reports_asset_and_index_for_a_resident_chunk() {
        let rsrc = test_resource();
        let asset = rsrc.define_test_asset();
        let req = rsrc.request(asset, 3, CHUNK_SIZE).unwrap();
        let c = req.chunk();
        rsrc.mark_ready(c);
        assert_eq!(rsrc.chunk_ident(c), Some((asset, 3)));
    }

    #[test]
    fn chunk_ident_is_none_for_a_non_resident_pointer() {
        let rsrc = test_resource();
        rsrc.define_test_asset(); // manager has at least one asset, but no resident chunks
        let bogus = Chunk::from_ptr(0x1234_usize as *mut u8).unwrap();
        assert_eq!(rsrc.chunk_ident(bogus), None);
    }

    #[test]
    fn pin_token_round_trips_retain_release_balance() {
        let rsrc = test_resource();
        let asset = rsrc.define_test_asset();
        let req = rsrc.request(asset, 0, CHUNK_SIZE).unwrap();
        let c = req.chunk();
        rsrc.mark_ready(c);
        let slot = rsrc.slot_of(c);
        let base = rsrc.lease_count_by_slot(slot);
        let token = rsrc.pin_token(c);
        assert_ne!(token, 0, "a resident chunk yields a nonzero token");
        rsrc.retain_token(token);
        assert_eq!(rsrc.lease_count_by_slot(slot), base + 1);
        rsrc.release_token(token);
        assert_eq!(rsrc.lease_count_by_slot(slot), base);
        // Zero token is a no-op both ways.
        rsrc.retain_token(0);
        rsrc.release_token(0);
        assert_eq!(rsrc.lease_count_by_slot(slot), base);
    }
}
