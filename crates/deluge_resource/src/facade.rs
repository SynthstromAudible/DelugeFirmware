//! A safe Rust facade over the residency manager: a typed chunk handle plus the
//! query/schedule operations, composing the SAME private `Manager` methods the
//! `unsafe extern "C"` wrappers in `manager.rs` call (exposed here via `pub(crate)`,
//! bodies unchanged). Every facade op that takes a manager-side lease returns the
//! RAII `Lease` guard, never a bare `Chunk` — so a leak requires deliberately
//! forgetting to drop the guard, not just forgetting to wrap one. The internal
//! `try_acquire` helper (private) still returns a bare `Chunk`, but its only callers
//! are `acquire_leased` here, which wraps it immediately.
//!
//! One `unsafe` fn at the edge (`Resource::from_handle`, mirroring the C ABI's
//! `mgr()`); everything past it is safe, `Option`-returning Rust.

use crate::manager::{mgr, Manager};
use crate::DelugeResource;
use core::ptr::NonNull;

/// A resident chunk's backing pointer — the manager's chunk identity. `Copy`, and
/// deliberately NOT a lease: it does not release anything on drop (see the future
/// `Lease` RAII guard). Carries no lifetime because the manager's tables are
/// process-lifetime (a boot singleton); staleness is caught by the manager's own
/// identity re-validation (e.g. `try_acquire`'s asset/index check), not by the type.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Chunk(NonNull<u8>);

impl Chunk {
    /// Map a manager-returned backing pointer to a typed handle: null (miss / OOM /
    /// not-ready) -> `None`, non-null -> `Some(Chunk)`.
    fn from_ptr(p: *mut u8) -> Option<Self> {
        NonNull::new(p).map(Chunk)
    }

    /// The raw backing pointer, for handing back to a manager method that still
    /// speaks pointers (or to the C++ side during the migration).
    pub fn as_ptr(self) -> *mut u8 {
        self.0.as_ptr()
    }
}

/// The safe entry point over a manager handle (a boot-singleton). Wraps `&'m Manager`
/// — a thin, `Copy`-able view, not an owner.
pub struct Resource<'m> {
    mgr: &'m Manager,
}

impl<'m> Resource<'m> {
    /// Build a `Resource` over the opaque `*mut DelugeResource` handle the app already
    /// holds (returned by `deluge_resource_create`/`_unhooked`).
    ///
    /// # Safety
    /// `h` must be non-null and a live handle previously returned by
    /// `deluge_resource_create`/`deluge_resource_create_unhooked`, valid for at least
    /// `'m`. This mirrors exactly the safety contract the `deluge_resource_*` C-ABI
    /// wrappers already rely on for every call through `h`.
    pub unsafe fn from_handle(h: *mut DelugeResource) -> Self {
        // SAFETY: forwarded to the caller's contract above — same cast `mgr()` performs
        // for the C-ABI wrappers.
        Resource {
            mgr: unsafe { mgr(h) },
        }
    }

    /// RT-safe, non-blocking: is chunk `index` of `asset` resident AND ready? Never
    /// allocates, materializes, or blocks. `None` on a miss (not resident, or resident
    /// but still `Loading` — see `mark_ready`). Takes a hard lease on a hit (mirrors
    /// `deluge_resource_try_acquire`). Private: returns a bare, non-RAII `Chunk`, so the
    /// only caller is `acquire_leased`, which wraps the lease it just took into a
    /// `Lease` before handing anything back. Public callers want `acquire_leased`.
    fn try_acquire(&self, asset: u32, index: u32) -> Option<Chunk> {
        Chunk::from_ptr(self.mgr.try_acquire(asset, index))
    }

    /// Non-leasing residency peek: `Some(Chunk)` if `index` of `asset` is RESIDENT,
    /// regardless of `ready`/loaded state — `None` only on a miss. Unlike
    /// `try_acquire`/`acquire_leased` this takes NO lease and does NOT bump recency,
    /// so it never perturbs `evict_lowest`'s victim choice. For a caller (e.g. the
    /// C++ `get_cluster` port) that needs to distinguish "already resident, don't
    /// double-allocate" from "not resident, must request" without pinning it or
    /// disturbing LRU order; callers that need loaded data still check the chunk's
    /// own ready/loaded flag (see `is_ready`) after the peek.
    pub fn peek(&self, asset: u32, index: u32) -> Option<Chunk> {
        Chunk::from_ptr(self.mgr.peek(asset, index))
    }

    /// Reserve + construct (no I/O; `ready = false`) chunk `index` of `asset`, so an
    /// external loader can fill it and call `mark_ready`, wrapping the one lease this
    /// takes into the RAII `Lease` guard. `None` on OOM, a full table with nothing
    /// evictable, or an asset with no `construct` callback attached (nothing was
    /// leased in that case). The returned `Lease` is the caller's owned pin on the
    /// reserved-but-not-yet-ready chunk — e.g. what a streaming cursor stores as its
    /// `pending` slot while the loader fills it; dropping it releases the reservation.
    pub fn request(&self, asset: u32, index: u32, size: usize) -> Option<Lease> {
        Chunk::from_ptr(self.mgr.request(asset, index, size))
            .map(|c| Lease::adopt(self.mgr as *const Manager, c))
    }

    /// Publish readiness on a `request`-ed chunk after its data has been filled — the
    /// loader/embassy storage task signals the read completed. No-op if the chunk's
    /// backing is no longer resident.
    pub fn mark_ready(&self, chunk: Chunk) {
        self.mgr.mark_ready(chunk.as_ptr());
    }

    /// The O(1) chunk-table slot index backing `chunk` (for lease-count queries / the
    /// loader queue, which key by slot rather than pointer).
    pub fn slot_of(&self, chunk: Chunk) -> u32 {
        self.mgr.slot_of(chunk.as_ptr())
    }

    /// The `(asset, index)` of a resident chunk backing `chunk` (the manager's `ChunkSlot`
    /// holds both). `None` if the pointer isn't resident. `asset == NONE` (`u32::MAX`) for
    /// an adopted chunk. Lets a caller that only has a `Chunk` (e.g. the native streaming
    /// fill task, working from `loader_next`'s return) recover which asset/cluster it is
    /// without threading the identity through separately.
    pub fn chunk_ident(&self, chunk: Chunk) -> Option<(u32, u32)> {
        self.mgr.chunk_ident(chunk.as_ptr())
    }

    /// Enqueue the chunk at `slot` for loading at `priority` (lower = more urgent;
    /// re-enqueue just updates the priority).
    pub fn loader_enqueue(&self, slot: u32, priority: u32) {
        self.mgr.loader_enqueue(slot, priority);
    }

    /// Remove the chunk at `slot` from the load queue (the `dequeue` primitive — cancels a pending
    /// load). No-op if `slot` isn't queued.
    pub fn loader_remove(&self, slot: u32) {
        self.mgr.loader_remove(slot);
    }

    /// Pop the most-urgent queued + still-leased chunk (clearing its queued flag), or
    /// `None` if the load queue is empty. The returned `Chunk` is a NON-OWNING borrow
    /// of an already-leased chunk (`loader_next` never leases — see `Manager::loader_next`,
    /// which only ever picks a `queued + still-leased` slot): the requester (whoever
    /// called `request`/`acquire_leased`) already owns the lease and its `Lease` guard,
    /// so the loader must use this handle to fill/`mark_ready` the chunk but must NOT
    /// wrap it in a `Lease` of its own — that would double-release on drop.
    pub fn loader_next(&self) -> Option<Chunk> {
        Chunk::from_ptr(self.mgr.loader_next())
    }

    /// O(1) hard-lease count of the chunk at `slot` (see `slot_of`) — 0 if `slot` is
    /// out of range or free. Exposed mainly for tests / invariant checks that want to
    /// observe lease balance directly (`Lease`'s whole point is that callers normally
    /// don't need to).
    pub fn lease_count_by_slot(&self, slot: u32) -> u32 {
        self.mgr.lease_count_by_slot(slot)
    }

    /// Take a hard-lease on `chunk`, returning the RAII guard. Exactly one
    /// `add_lease` per `Lease` returned — `Drop` releases it exactly once (see
    /// `Lease`).
    pub fn lease(&self, chunk: Chunk) -> Lease {
        self.mgr.add_lease(chunk.as_ptr());
        // SAFETY-of-soundness (not an `unsafe` block, just documenting the pointer):
        // `self.mgr` is a live `&Manager` right here, so casting it to a raw pointer
        // for `Lease` to carry is sound — see the `Lease` doc for why the pointer
        // stays valid for the guard's whole (possibly much longer than `'m`) lifetime.
        Lease::adopt(self.mgr as *const Manager, chunk)
    }

    /// Convenience: `try_acquire` + wrap the lease it already took into the RAII guard,
    /// in one step (the common "acquire a resident region" call). `try_acquire` itself
    /// takes exactly one hard lease on a hit (see its doc) — this does NOT call `lease`
    /// on top (that would double-lease the chunk while the guard only releases once,
    /// leaking a lease on every call). `None` on a miss (nothing was leased).
    pub fn acquire_leased(&self, asset: u32, index: u32) -> Option<Lease> {
        self.try_acquire(asset, index)
            .map(|c| Lease::adopt(self.mgr as *const Manager, c))
    }

    /// Pack a resident chunk's `{slot, generation}` into the opaque independent-pin
    /// token the region port hands callers (`DelugeSampleRegion::lease`). `0` if the
    /// chunk is not resident (no valid handle). Callers never introspect it — they
    /// only feed it back to `retain_token`/`release_token`.
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

    /// Take an independent pin keyed on `token` alone (generation-checked). No-op on
    /// `token == 0` or a stale token (the slot was evicted+reused since minting). This
    /// is the deliberately-manual external pin (its owner is the C++ reader across the
    /// C ABI), distinct from the RAII `Lease` guarding the cursor's own slots.
    pub fn retain_token(&self, token: u64) {
        if token == 0 {
            return;
        }
        let slot = (token >> 32) as u32;
        let gen = token as u32;
        self.mgr.retain_by_slot_gen(slot, gen);
    }

    /// Drop an independent pin taken via `retain_token` (generation-checked; no-op on
    /// 0/stale).
    pub fn release_token(&self, token: u64) {
        if token == 0 {
            return;
        }
        let slot = (token >> 32) as u32;
        let gen = token as u32;
        self.mgr.release_by_slot_gen(slot, gen);
    }

    /// Live readiness of a held chunk (the state query needs to see a `Loading` chunk
    /// that has since landed). `false` if the chunk is no longer resident.
    pub fn is_ready(&self, chunk: Chunk) -> bool {
        self.mgr.is_ready_by_ptr(chunk.as_ptr())
    }
}

/// A held hard-lease on a resident chunk. `Drop` releases it exactly once — the lease
/// balance is enforced by ownership, not by hand (in place of the C++ side's paired
/// `addReason`/`removeReason` calls, whose imbalance was a real, expensive-to-review
/// bug class; RAII makes that class impossible here). Non-`Copy`, non-`Clone`: only
/// the guard that took the lease may release it.
///
/// Holds the manager as a raw `*const Manager`, not `&'m Manager` — so `Lease` carries
/// NO lifetime parameter and is freely storable in a `Cell<Option<Lease>>` (a per-slot
/// streaming cursor's state, which lives far longer than any single `Resource<'m>`
/// borrow used to create the lease). This is sound because the manager is a
/// boot-singleton: `deluge_resource_create`/`_unhooked` allocates it once from the
/// heap and it is never freed or moved for the remaining life of the program (see
/// `create_inner`), so the pointer stays valid for as long as any `Lease` can exist —
/// exactly the same "process-lifetime, no dereference needed to check" argument
/// `Chunk` already relies on for its own pointer.
pub struct Lease {
    mgr: *const Manager,
    chunk: Chunk,
}

impl Lease {
    /// Wrap an already-taken manager-side lease on `chunk` into the RAII guard,
    /// without calling `add_lease` again. Private: the only callers are
    /// `Resource::lease` (right after its own `add_lease`) and
    /// `Resource::acquire_leased` (right after `try_acquire`'s own lease) — each site
    /// is responsible for having taken exactly the one lease this guard will release.
    fn adopt(mgr: *const Manager, chunk: Chunk) -> Self {
        debug_assert!(!mgr.is_null(), "Lease over a null manager pointer");
        Lease { mgr, chunk }
    }

    /// The leased chunk's handle (e.g. to hand its pointer to a materialize callback,
    /// or to look up its slot for the loader queue).
    pub fn chunk(&self) -> Chunk {
        self.chunk
    }
}

impl Drop for Lease {
    fn drop(&mut self) {
        // SAFETY: `self.mgr` was derived from a live `&Manager` at construction
        // (`Resource::lease`/`acquire_leased`, both called through a live `Resource<'m>`)
        // and the manager is a boot-singleton that outlives every `Lease` (see the
        // struct doc) — so the pointer is still valid here, however long this guard
        // lived. `Manager::release` already enters its own masked critical section
        // (`rmw_by_ptr` -> `Masked::enter`), so this is ISR/main-thread safe with no
        // extra locking added here; `no_std` aborts on panic, so there is no unwind
        // path that could run this drop a second time or interleave a partial one.
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

    /// A manager over a throwaway test heap, mirroring the harness in `testing.rs`
    /// and the `lib.rs` unit tests (a 16-aligned `Vec<u128>` arena kept alive
    /// alongside the handle, one requestable test asset defined lazily).
    struct TestResource {
        _buf: Vec<u128>,
        handle: *mut DelugeResource,
    }

    fn test_resource() -> TestResource {
        let words = (256 * 1024usize).div_ceil(16);
        let mut buf = std::vec![0u128; words];
        // SAFETY: `buf` is a live, 16-aligned, `words * 16`-byte arena for the whole
        // test's duration (kept alive in `TestResource::_buf`).
        let h =
            unsafe { deluge_alloc::deluge_heap_create(buf.as_mut_ptr() as *mut u8, words * 16) };
        // SAFETY: `h` is the live heap handle just created above.
        let handle = unsafe { crate::deluge_resource_create(h, 4, 16) };
        assert!(!handle.is_null());
        TestResource { _buf: buf, handle }
    }

    impl TestResource {
        fn resource(&self) -> Resource<'_> {
            // SAFETY: `self.handle` is a live handle for as long as `self` (and its
            // `_buf`) is alive.
            unsafe { Resource::from_handle(self.handle) }
        }

        /// Define + make requestable a single test asset.
        fn define_test_asset(&self) -> u32 {
            // SAFETY: `self.handle` is live; `mock_materialize`/`mock_construct` have
            // the required C-ABI signature.
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
        let rsrc = test_resource(); // build a manager over a test heap (mirror testing.rs)
        let asset = rsrc.define_test_asset();
        assert!(rsrc.acquire_leased(asset, 0).is_none()); // not resident yet
        let req = rsrc.request(asset, 0, CHUNK_SIZE).expect("request");
        let c = req.chunk();
        rsrc.mark_ready(c);
        let got = rsrc.acquire_leased(asset, 0).expect("now ready");
        assert_eq!(got.chunk(), c); // same chunk identity
                                    // `req` and `got` each hold their own lease; both release on drop below.
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
        // Regression for the double-lease trap: `acquire_leased` must NOT call
        // `try_acquire` (which already leases on a hit) and then `lease()` (which
        // would lease a second time while the guard only ever releases once).
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

    /// `peek` is `try_acquire` minus the mutation: resident (ready OR not — see the
    /// `Manager::peek` doc, it is deliberately NOT ready-gated) reports the backing;
    /// not-resident reports `None`; and unlike `try_acquire` it takes NO lease and
    /// does NOT bump recency, so it must not perturb `evict_lowest`'s victim choice.
    #[test]
    fn peek_is_resident_not_ready_gated_and_perturbs_neither_leases_nor_eviction_order() {
        let rsrc = test_resource();
        let asset = rsrc.define_test_asset();

        // Not resident -> None.
        assert!(rsrc.resource().peek(asset, 0).is_none());

        // Resident but NOT ready (`request` leaves `ready = false` until `mark_ready`).
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

        // c0 is older -> evict_lowest would pick it first, with or without peeking
        // it repeatedly in between (a bugged peek that bumped recency would make c0
        // look newer than c1 and flip the victim).
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

    /// Compile-check (and a real drop-cycle exercise): `Lease` carries no lifetime, so
    /// it must be storable in a `Cell<Option<Lease>>` — a per-slot streaming cursor's
    /// state, which lives far longer than any single `Resource<'m>` borrow used to
    /// create the lease. `Cell::take` moves the guard out (leaving `None`); dropping the taken
    /// value releases the lease, exactly like the cursor's state-transition `take`s will.
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

    /// The RAII cursor stores `Cell<Option<Lease>>` per slot and drops a `Lease`
    /// (via `take`/`replace`) from BOTH the audio-ISR path
    /// (`acquire`) and the main path (`close`). This proves `Lease::drop ->
    /// Manager::release` composes with the manager's asymmetric masked discipline
    /// (`sync::Masked`) exactly right in each context — it decrements exactly once
    /// either way, and only the main path takes the critical section (the ISR path
    /// is lock-free by the manager's own atomic-vs-fiber guarantee). Uses the same
    /// `sync::stubs` two-context model (`audio_isr_context_set`) + critical-section
    /// counters that `sync.rs`'s own `fiber_masks_rmw` / `audio_skips_mask` tests use.
    ///
    /// `cargo test`'s critical-section counters are thread-local (see `sync::stubs`),
    /// so this single-threaded test — flipping the in-interrupt flag to model the two
    /// contexts, the established in-crate pattern — reads only its own thread's counts.
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

        // (a) MAIN context: dropping a Lease enters the masked critical section
        // (release goes through `rmw_by_ptr` -> `Masked::enter`) and decrements once.
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

        // (b) AUDIO-ISR context: dropping a Lease must NOT mask (the audio path is
        // already atomic w.r.t. the fiber, so the manager skips the lock) yet must
        // still decrement exactly once.
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
        // Read still in ISR context (also lock-free) so the count assertion above holds.
        assert_eq!(
            rsrc.lease_count_by_slot(slot),
            base,
            "lock-free ISR release decremented exactly one lease"
        );

        stubs::audio_isr_context_set(false); // restore for other tests on this thread
    }

    /// The cross-slot non-corruption fact: the RAII cursor drops leases from the main
    /// path and the ISR path on DIFFERENT cursor slots. Interleaving a main-context
    /// lease/drop on slot0 with an ISR-context lease/drop on slot1 must leave EACH
    /// slot's count decremented by exactly its own lease — a masked release on one
    /// slot never corrupts the lock-free release on another (and vice versa).
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

    /// A `request`-ed chunk's `chunk_ident` reports the same `(asset, index)` it was
    /// requested with — the manager's `ChunkSlot` carries both, so this is a pure
    /// readback, not a re-derivation.
    #[test]
    fn chunk_ident_reports_asset_and_index_for_a_resident_chunk() {
        let rsrc = test_resource();
        let asset = rsrc.define_test_asset();
        let req = rsrc.request(asset, 3, CHUNK_SIZE).unwrap();
        let c = req.chunk();
        rsrc.mark_ready(c);
        assert_eq!(rsrc.chunk_ident(c), Some((asset, 3)));
    }

    /// A pointer the manager never handed out (never resident) reports `None`, not a
    /// stale/garbage identity.
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
