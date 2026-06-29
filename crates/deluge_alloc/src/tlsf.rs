//! Two-Level Segregated Fit (TLSF) allocator core — O(1) bounded alloc/free with
//! bounded fragmentation, `no_std`, no MMU, all metadata in the managed region.
//!
//! From-scratch implementation of the canonical TLSF algorithm (Masmano et al.;
//! structure follows Matt Conte's reference `tlsf.c`).
//!
//! Invariants this guarantees by construction (the reason for the rewrite):
//! - every payload pointer is **16-byte aligned** (granularity == 16, headers
//!   padded to 16), and `alloc` honors any larger power-of-two alignment;
//! - adjacent free blocks always coalesce (boundary tags);
//! - alloc/free are O(1) worst-case (segregated free lists + bitmaps).

use core::ffi::c_void;
use core::ptr;

/// Reclaim hook: invoked when the heap is out of space, to ask a resource layer
/// (the slab cache pool) to free `bytes_needed` worth of reclaimable memory.
/// Returns true if it freed something (the alloc is then retried). Mirrors the
/// C++ `Reclaimer` seam (Phase 1) on the Rust side.
pub type ReclaimFn = unsafe extern "C" fn(ctx: *mut c_void, bytes_needed: usize) -> bool;

const ALIGN_LOG2: usize = 4;
pub const ALIGN: usize = 1 << ALIGN_LOG2; // 16
const SL_INDEX_COUNT_LOG2: usize = 5;
const SL_INDEX_COUNT: usize = 1 << SL_INDEX_COUNT_LOG2; // 32
const FL_INDEX_MAX: usize = 30; // up to 1 GiB (covers the 64 MB SDRAM arena)
const FL_INDEX_SHIFT: usize = SL_INDEX_COUNT_LOG2 + ALIGN_LOG2; // 9
const FL_INDEX_COUNT: usize = FL_INDEX_MAX - FL_INDEX_SHIFT + 1; // 22
const SMALL_BLOCK_SIZE: usize = 1 << FL_INDEX_SHIFT; // 512

const BLOCK_FREE: usize = 1 << 0;
const BLOCK_PREV_FREE: usize = 1 << 1;
const FLAG_MASK: usize = BLOCK_FREE | BLOCK_PREV_FREE;
const SIZE_MASK: usize = !FLAG_MASK;

/// Per-block boundary tag — *exactly* `HEADER_SIZE` (16) bytes. Padded to 16 so
/// payloads (which start immediately after) are 16-aligned on every arch. The
/// doubly-linked free-list links are NOT struct fields: while a block is free they
/// live in the first two words of its *payload* (see `link_next`/`link_prev`). This
/// matters for soundness — keeping the struct at 16 bytes means a `&BlockHeader` (or
/// any `&self`/`&mut self` method) never spans more than the 16 bytes actually
/// reserved for a header, so forming one for the end sentinel (which has only a
/// header, no payload) stays in-bounds. (A 32-byte struct made the sentinel's
/// reference run past the arena — UB, caught by miri.)
#[repr(C, align(16))]
struct BlockHeader {
    /// Physical-predecessor block; valid only when this block's PREV_FREE is set.
    prev_phys: *mut BlockHeader,
    /// Payload size in bytes (multiple of ALIGN) in the high bits; low 2 bits are
    /// the BLOCK_FREE / BLOCK_PREV_FREE flags.
    size_and_flags: usize,
}

const HEADER_SIZE: usize = 16; // align(16) padding guarantees this on 32- and 64-bit.

/// Smallest payload a free block must hold (its two free-list links).
const MIN_BLOCK_SIZE: usize = 16;

#[inline]
fn fls(word: usize) -> usize {
    // index of the most significant set bit; word must be non-zero.
    (usize::BITS - 1 - word.leading_zeros()) as usize
}

impl BlockHeader {
    #[inline]
    fn size(&self) -> usize {
        self.size_and_flags & SIZE_MASK
    }
    #[inline]
    fn set_size(&mut self, size: usize) {
        self.size_and_flags = size | (self.size_and_flags & FLAG_MASK);
    }
    #[inline]
    fn is_free(&self) -> bool {
        self.size_and_flags & BLOCK_FREE != 0
    }
    #[inline]
    fn is_prev_free(&self) -> bool {
        self.size_and_flags & BLOCK_PREV_FREE != 0
    }
    #[inline]
    fn set_free(&mut self) {
        self.size_and_flags |= BLOCK_FREE;
    }
    #[inline]
    fn set_used(&mut self) {
        self.size_and_flags &= !BLOCK_FREE;
    }
    #[inline]
    fn set_prev_free(&mut self) {
        self.size_and_flags |= BLOCK_PREV_FREE;
    }
    #[inline]
    fn set_prev_used(&mut self) {
        self.size_and_flags &= !BLOCK_PREV_FREE;
    }
}

#[inline]
unsafe fn payload_of(block: *mut BlockHeader) -> *mut u8 {
    (block as *mut u8).add(HEADER_SIZE)
}

#[inline]
unsafe fn block_of(payload: *mut u8) -> *mut BlockHeader {
    payload.sub(HEADER_SIZE) as *mut BlockHeader
}

#[inline]
unsafe fn next_phys(block: *mut BlockHeader) -> *mut BlockHeader {
    (payload_of(block)).add((*block).size()) as *mut BlockHeader
}

// Doubly-linked free-list links, stored in the payload of a *free* block (which is
// always >= MIN_BLOCK_SIZE = two pointers). Kept out of `BlockHeader` so the struct
// stays exactly HEADER_SIZE; only ever touched on free blocks, so the payload bytes
// they occupy are always in-bounds.
#[inline]
unsafe fn link_next(block: *mut BlockHeader) -> *mut BlockHeader {
    (payload_of(block) as *mut *mut BlockHeader).read()
}
#[inline]
unsafe fn set_link_next(block: *mut BlockHeader, v: *mut BlockHeader) {
    (payload_of(block) as *mut *mut BlockHeader).write(v);
}
#[inline]
unsafe fn link_prev(block: *mut BlockHeader) -> *mut BlockHeader {
    (payload_of(block) as *mut *mut BlockHeader).add(1).read()
}
#[inline]
unsafe fn set_link_prev(block: *mut BlockHeader, v: *mut BlockHeader) {
    (payload_of(block) as *mut *mut BlockHeader).add(1).write(v);
}

// ---------------------------------------------------------------------------
// AddressSanitizer poisoning (host-sim use-after-free / use-after-steal).
//
// With the `asan` feature on, a free (or never-yet-carved) block's payload is
// poisoned so any access traps — EXCEPT its first `LINK_BYTES` (the two free-list
// links the allocator parks there) and its 16-byte header, which the allocator
// must keep touching. malloc unpoisons the whole carved block before returning it.
// Off, every hook compiles to nothing, so the device codegen is unchanged.
//
// The __asan_* runtime symbols resolve from libasan at the final C++ link (the sim
// links -fsanitize=address); this staticlib leaves them undefined, which is fine.
// ---------------------------------------------------------------------------

/// Bytes at a free block's payload start reserved for its two free-list links.
const LINK_BYTES: usize = 2 * core::mem::size_of::<*mut BlockHeader>();

#[cfg(feature = "asan")]
extern "C" {
    fn __asan_poison_memory_region(addr: *const c_void, size: usize);
    fn __asan_unpoison_memory_region(addr: *const c_void, size: usize);
}

#[inline(always)]
unsafe fn asan_poison(_addr: *mut u8, _size: usize) {
    #[cfg(feature = "asan")]
    if _size != 0 {
        __asan_poison_memory_region(_addr as *const c_void, _size);
    }
}

#[inline(always)]
unsafe fn asan_unpoison(_addr: *mut u8, _size: usize) {
    #[cfg(feature = "asan")]
    if _size != 0 {
        __asan_unpoison_memory_region(_addr as *const c_void, _size);
    }
}

/// Poison the body of a now-free `block`, leaving its two free-list links (the
/// only bytes the allocator reads/writes while it sits free) accessible.
#[inline(always)]
unsafe fn poison_free_body(block: *mut BlockHeader) {
    let size = (*block).size();
    if size > LINK_BYTES {
        asan_poison(payload_of(block).add(LINK_BYTES), size - LINK_BYTES);
    }
}

/// Unpoison the full payload of `block` (its final, post-split size) — called
/// just before handing the block to the caller.
#[inline(always)]
unsafe fn unpoison_block(block: *mut BlockHeader) {
    asan_unpoison(payload_of(block), (*block).size());
}

/// (first-level, second-level) index for a block of `size`, rounded *down*
/// (used when inserting a free block).
#[inline]
fn mapping_insert(size: usize) -> (usize, usize) {
    if size < SMALL_BLOCK_SIZE {
        (0, size >> ALIGN_LOG2)
    } else {
        let fl = fls(size);
        let sl = (size >> (fl - SL_INDEX_COUNT_LOG2)) ^ SL_INDEX_COUNT;
        (fl - (FL_INDEX_SHIFT - 1), sl)
    }
}

/// (fl, sl) rounded *up* to the next size class (used when searching for a block
/// to satisfy an allocation), so the found block is guaranteed large enough.
#[inline]
fn mapping_search(size: usize) -> (usize, usize) {
    let size = if size >= SMALL_BLOCK_SIZE {
        let round = (1usize << (fls(size) - SL_INDEX_COUNT_LOG2)) - 1;
        size + round
    } else {
        size
    };
    mapping_insert(size)
}

pub struct Tlsf {
    fl_bitmap: u32,
    sl_bitmap: [u32; FL_INDEX_COUNT],
    blocks: [[*mut BlockHeader; SL_INDEX_COUNT]; FL_INDEX_COUNT],
    // Reclaim hook (the slab cache pool registers here). All-zero after init()
    // means "no hook" (Option<fn> null niche, null ctx, not reclaiming).
    reclaim_cb: Option<ReclaimFn>,
    reclaim_ctx: *mut c_void,
    reclaiming: bool,
}

impl Tlsf {
    /// Construct an empty TLSF in place at `ptr` (must point to writable storage
    /// of at least `size_of::<Tlsf>()` bytes).
    pub unsafe fn init(ptr: *mut Tlsf) {
        ptr::write_bytes(ptr as *mut u8, 0, core::mem::size_of::<Tlsf>());
    }

    pub const fn control_size() -> usize {
        core::mem::size_of::<Tlsf>()
    }

    pub fn set_reclaim(&mut self, cb: ReclaimFn, ctx: *mut c_void) {
        self.reclaim_cb = Some(cb);
        self.reclaim_ctx = ctx;
    }

    /// The registered hook, if any. Returned by value so the caller can drop its
    /// borrow of the heap before invoking it (the hook re-enters via free()).
    pub fn reclaim_hook(&self) -> Option<(ReclaimFn, *mut c_void)> {
        self.reclaim_cb.map(|cb| (cb, self.reclaim_ctx))
    }

    pub fn is_reclaiming(&self) -> bool {
        self.reclaiming
    }
    pub fn set_reclaiming(&mut self, v: bool) {
        self.reclaiming = v;
    }

    #[inline]
    fn null() -> *mut BlockHeader {
        ptr::null_mut()
    }

    unsafe fn insert_free_block(&mut self, block: *mut BlockHeader) {
        let (fl, sl) = mapping_insert((*block).size());
        let head = self.blocks[fl][sl];
        set_link_next(block, head);
        set_link_prev(block, Self::null());
        if !head.is_null() {
            set_link_prev(head, block);
        }
        self.blocks[fl][sl] = block;
        self.fl_bitmap |= 1u32 << fl;
        self.sl_bitmap[fl] |= 1u32 << sl;
    }

    unsafe fn remove_free_block(&mut self, block: *mut BlockHeader) {
        let (fl, sl) = mapping_insert((*block).size());
        let prev = link_prev(block);
        let next = link_next(block);
        if !next.is_null() {
            set_link_prev(next, prev);
        }
        if !prev.is_null() {
            set_link_next(prev, next);
        }
        if self.blocks[fl][sl] == block {
            self.blocks[fl][sl] = next;
            if next.is_null() {
                self.sl_bitmap[fl] &= !(1u32 << sl);
                if self.sl_bitmap[fl] == 0 {
                    self.fl_bitmap &= !(1u32 << fl);
                }
            }
        }
    }

    /// Find a free block >= the size implied by (fl, sl); returns null if none.
    unsafe fn search_suitable(&self, fl_in: usize, sl_in: usize) -> *mut BlockHeader {
        let mut fl = fl_in;
        let mut sl = sl_in;
        // second-level bits at or above sl within this fl
        let mut sl_map = self.sl_bitmap[fl] & (!0u32 << sl);
        if sl_map == 0 {
            // move up to the next first-level list with any free block
            let fl_map = self.fl_bitmap & (!0u32 << (fl + 1));
            if fl_map == 0 {
                return Self::null();
            }
            fl = fl_map.trailing_zeros() as usize;
            sl_map = self.sl_bitmap[fl];
        }
        sl = sl_map.trailing_zeros() as usize;
        self.blocks[fl][sl]
    }

    /// Mark `block` used, removing it from its free list, and split off any
    /// remainder beyond `size` as a new free block.
    unsafe fn use_block(&mut self, block: *mut BlockHeader, size: usize) {
        self.remove_free_block(block);
        // Unpoison the whole block first: split() writes the remainder's header into
        // what was this block's poisoned body. split() then re-poisons the remainder.
        unpoison_block(block);
        self.split(block, size);
        (*block).set_used();
        // tell the (used) physical successor its prev is no longer free
        let next = next_phys(block);
        (*next).set_prev_used();
    }

    /// If `block` is larger than `size` by at least a minimum block, split the
    /// remainder into a new free block and register it.
    unsafe fn split(&mut self, block: *mut BlockHeader, size: usize) {
        let block_size = (*block).size();
        if block_size >= size + HEADER_SIZE + MIN_BLOCK_SIZE {
            let remainder_size = block_size - size - HEADER_SIZE;
            (*block).set_size(size);
            let remainder = next_phys(block);
            (*remainder).prev_phys = block;
            (*remainder).size_and_flags = remainder_size; // free=0 prev_free=0, fixed below
            (*remainder).set_free();
            (*remainder).set_prev_used();
            // successor of remainder records that its prev (remainder) is free
            let after = next_phys(remainder);
            (*after).prev_phys = remainder;
            (*after).set_prev_free();
            self.insert_free_block(remainder);
            poison_free_body(remainder); // remainder is now free
        }
    }

    /// Coalesce `block` with its physical predecessor and successor if free.
    /// Returns the (possibly new) merged block, registered as free.
    unsafe fn free_block(&mut self, mut block: *mut BlockHeader) {
        (*block).set_free();
        // merge with next if free
        let next = next_phys(block);
        if (*next).is_free() {
            self.remove_free_block(next);
            let new_size = (*block).size() + (*next).size() + HEADER_SIZE;
            (*block).set_size(new_size);
        }
        // merge with prev if free
        if (*block).is_prev_free() {
            let prev = (*block).prev_phys;
            self.remove_free_block(prev);
            let new_size = (*prev).size() + (*block).size() + HEADER_SIZE;
            (*prev).set_size(new_size);
            block = prev;
        }
        // fix successor links: its prev (block) is free
        let after = next_phys(block);
        (*after).prev_phys = block;
        (*after).set_prev_free();
        self.insert_free_block(block);
        // Poison the whole merged body (the freed user bytes plus any absorbed
        // neighbours' former headers/links — all interior now). The link area
        // insert_free_block just wrote stays accessible.
        poison_free_body(block);
    }

    /// Add a usable memory region to the pool. The control block (`Tlsf`) must
    /// live elsewhere (e.g. at the start of the arena, before `mem`).
    pub unsafe fn add_pool(&mut self, mem: *mut u8, size: usize) {
        debug_assert!((mem as usize).is_multiple_of(ALIGN));
        // Layout: [free block header][... payload ...][sentinel header]
        // The trailing zero-size used sentinel terminates coalescing.
        let pool_overhead = 2 * HEADER_SIZE;
        if size < pool_overhead + MIN_BLOCK_SIZE {
            return;
        }
        let block = mem as *mut BlockHeader;
        let block_size = (size - pool_overhead) & SIZE_MASK;
        (*block).prev_phys = Self::null();
        (*block).size_and_flags = block_size;
        (*block).set_free();
        (*block).set_prev_used();
        // sentinel: zero-size, used, immediately after the free block's payload
        let sentinel = next_phys(block);
        (*sentinel).prev_phys = block;
        (*sentinel).size_and_flags = 0;
        (*sentinel).set_used();
        (*sentinel).set_prev_free();
        self.insert_free_block(block);
        // Whole pool starts free: poison its body so accesses to not-yet-carved
        // space trap too (malloc unpoisons each block as it hands it out).
        poison_free_body(block);
    }

    /// Round a request up to the granularity and the free-block minimum.
    #[inline]
    fn adjust_size(size: usize) -> usize {
        let s = (size + ALIGN - 1) & !(ALIGN - 1);
        if s < MIN_BLOCK_SIZE {
            MIN_BLOCK_SIZE
        } else {
            s
        }
    }

    pub unsafe fn malloc(&mut self, size: usize, align: usize) -> *mut u8 {
        if size == 0 {
            return ptr::null_mut();
        }
        let align = align.max(ALIGN);
        let adjust = Self::adjust_size(size);

        if align == ALIGN {
            let (fl, sl) = mapping_search(adjust);
            let block = self.search_suitable(fl, sl);
            if block.is_null() {
                return ptr::null_mut();
            }
            self.use_block(block, adjust); // unpoisons the carved block for the caller
            return payload_of(block);
        }

        // Over-aligned: reserve room to shift the payload up to an aligned
        // boundary. The leading gap, if any, must become a *valid* free block
        // (header + >= MIN_BLOCK_SIZE payload), so reserve at most
        // align + HEADER + MIN_BLOCK_SIZE of slack. A free block from search has
        // only used physical neighbours (TLSF never leaves two free blocks
        // adjacent), so the leading remnant can't coalesce — just insert it.
        let extra = align + HEADER_SIZE + MIN_BLOCK_SIZE;
        let (fl, sl) = mapping_search(adjust + extra);
        let block = self.search_suitable(fl, sl);
        if block.is_null() {
            return ptr::null_mut();
        }
        self.remove_free_block(block);
        // The manual carve below writes headers/links across this block's body, so
        // unpoison it first; the leading remnant (if any) and the split tail are
        // re-poisoned as they become free.
        unpoison_block(block);

        let payload = payload_of(block) as usize;
        let mut aligned = (payload + align - 1) & !(align - 1);
        let mut gap = aligned - payload;
        // The leading block needs HEADER + MIN_BLOCK_SIZE bytes; if the gap is
        // smaller (but non-zero) push the payload up one more alignment step.
        if gap != 0 && gap < HEADER_SIZE + MIN_BLOCK_SIZE {
            aligned += align;
            gap = aligned - payload;
        }

        let mut block = block;
        if gap != 0 {
            // carve [block = leading free remnant][aligned_block = used ...]
            let total = (*block).size();
            let leading_size = gap - HEADER_SIZE;
            (*block).set_size(leading_size);
            let aligned_block = next_phys(block); // == block + gap
            (*aligned_block).prev_phys = block;
            (*aligned_block).size_and_flags = total - gap; // used, flags set below
            (*aligned_block).set_prev_free(); // its prev (leading) is free
            (*block).set_free();
            self.insert_free_block(block);
            poison_free_body(block); // leading remnant is now free
            block = aligned_block;
        }

        self.split(block, adjust);
        (*block).set_used();
        let next = next_phys(block);
        (*next).set_prev_used();
        debug_assert!((payload_of(block) as usize).is_multiple_of(align));
        payload_of(block)
    }

    pub unsafe fn free(&mut self, payload: *mut u8) {
        if payload.is_null() {
            return;
        }
        let block = block_of(payload);
        self.free_block(block);
    }

    pub unsafe fn usable_size(&self, payload: *mut u8) -> usize {
        if payload.is_null() {
            return 0;
        }
        (*block_of(payload)).size()
    }

    /// Split `block` (which is USED) down to `want` payload bytes, freeing the
    /// tail, when the excess is large enough to be its own block. Pointer-stable.
    unsafe fn shrink_in_place(&mut self, block: *mut BlockHeader, want: usize) {
        let cur = (*block).size();
        if cur < want + HEADER_SIZE + MIN_BLOCK_SIZE {
            return; // not enough excess to carve a free block; keep as-is
        }
        let tail_size = cur - want - HEADER_SIZE;
        (*block).set_size(want);
        let tail = next_phys(block);
        (*tail).prev_phys = block;
        (*tail).size_and_flags = tail_size; // used + prev_used; freed below
        self.free_block(tail); // marks free, coalesces with the following block, inserts
    }

    /// Resize, preserving the leading min(old, new) bytes. A **shrink** is done in
    /// place (split the tail, pointer-stable, no copy — and no worse for
    /// fragmentation than moving, since the freed tail still coalesces with any
    /// following free block). This covers the only remaining in-place-resize
    /// callers (wave_table / patch_cable_set trims), which all shrink. A **grow**
    /// has no in-place consumer (ResizeableArray, the lone `extend` user, is gone),
    /// so it just moves (alloc + copy + free) — correct, only unoptimized.
    pub unsafe fn realloc(&mut self, payload: *mut u8, new_size: usize, align: usize) -> *mut u8 {
        if payload.is_null() {
            return self.malloc(new_size, align);
        }
        if new_size == 0 {
            self.free(payload);
            return ptr::null_mut();
        }
        let block = block_of(payload);
        let cur = (*block).size();
        let want = Self::adjust_size(new_size);
        let need_align = align.max(ALIGN);
        let aligned_ok = (payload as usize) & (need_align - 1) == 0;

        // Shrink in place (pointer-stable). Growth falls through to a move.
        if aligned_ok && want <= cur {
            self.shrink_in_place(block, want);
            return payload;
        }
        let np = self.malloc(new_size, align);
        if np.is_null() {
            return ptr::null_mut(); // original left intact
        }
        ptr::copy_nonoverlapping(payload, np, cur.min(new_size));
        self.free(payload);
        np
    }
}
