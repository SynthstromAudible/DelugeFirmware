//! Two-Level Segregated Fit (TLSF) allocator core: O(1) alloc/free with bounded fragmentation,
//! `no_std`, all metadata in the managed region.
//!
//! Follows the canonical algorithm (Masmano et al.) and the structure of Matt Conte's `tlsf.c`.
//! Invariants, by construction:
//!
//! - every payload is 16-byte aligned (granularity 16, headers padded to 16), and `malloc` honours
//!   any larger power-of-two alignment;
//! - no two free blocks are physically adjacent (boundary-tag coalescing);
//! - alloc/free are O(1) worst case (segregated free lists plus bitmaps).

use core::ffi::c_void;
use core::ptr;

/// Reclaim hook, called when the heap is out of space to ask the cache layer to free
/// `bytes_needed` bytes of reclaimable memory. Returns true if it freed something, and the
/// allocation is retried.
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

/// Per-block boundary tag, exactly `HEADER_SIZE` (16) bytes, so payloads are 16-aligned on every
/// architecture.
///
/// The free-list links are not fields: while a block is free they live in the first two words of
/// its payload (`link_next`/`link_prev`). Keeping the struct at 16 bytes is a soundness
/// requirement — a `&BlockHeader` must never span past the header, because the end sentinel has
/// no payload and a larger struct would reach beyond the arena.
#[repr(C, align(16))]
struct BlockHeader {
    /// Physical-predecessor block; valid only when this block's PREV_FREE is set.
    prev_phys: *mut BlockHeader,
    /// Payload size in bytes (a multiple of `ALIGN`); the low 2 bits are the `BLOCK_FREE` and
    /// `BLOCK_PREV_FREE` flags.
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

// Free-list links, stored in a free block's payload (always at least `MIN_BLOCK_SIZE`, two
// pointers). Only touched on free blocks, so the bytes are always in bounds.
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

// AddressSanitizer poisoning.
//
// With the `asan` feature, a free or never-carved block's payload is poisoned so any access traps,
// except its header and its first `LINK_BYTES` (the free-list links), which the allocator keeps
// touching. `malloc` unpoisons the whole carved block before returning it. Without the feature
// every hook compiles to nothing. The `__asan_*` symbols are left undefined here, for the
// `-fsanitize=address` link to resolve.

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

/// Poisons the body of a now-free `block`, leaving its free-list links accessible.
#[inline(always)]
unsafe fn poison_free_body(block: *mut BlockHeader) {
    let size = (*block).size();
    if size > LINK_BYTES {
        asan_poison(payload_of(block).add(LINK_BYTES), size - LINK_BYTES);
    }
}

/// Unpoisons the full payload of `block` at its current size.
#[inline(always)]
unsafe fn unpoison_block(block: *mut BlockHeader) {
    asan_unpoison(payload_of(block), (*block).size());
}

/// Returns the (first-level, second-level) index for a free block of `size`, rounded down.
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

/// Returns the (fl, sl) index rounded up to the next size class, so any block found there is large
/// enough for `size`.
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

/// A TLSF control block, placed in front of the pool it manages.
pub struct Tlsf {
    fl_bitmap: u32,
    sl_bitmap: [u32; FL_INDEX_COUNT],
    blocks: [[*mut BlockHeader; SL_INDEX_COUNT]; FL_INDEX_COUNT],
    // All-zero after `init` means no hook and not reclaiming.
    reclaim_cb: Option<ReclaimFn>,
    reclaim_ctx: *mut c_void,
    reclaiming: bool,
    // The one pool's first block and end address, for `check`; null/0 until `add_pool`.
    pool_first: *mut BlockHeader,
    pool_end: usize,
    // Payload bytes on the free lists, and the least that has been after any malloc: the heap's
    // free space and its low-water mark, kept at the two free-list choke points.
    free_bytes: usize,
    min_free_bytes: usize,
}

/// A heap's occupancy, as [`Tlsf::stats`] reports it.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Stats {
    /// Bytes the pool spans, headers and sentinel included.
    pub pool_bytes: usize,
    /// Payload bytes of the free blocks now.
    pub free_bytes: usize,
    /// The least `free_bytes` has been after any allocation since the pool was added.
    pub min_free_bytes: usize,
}

impl Tlsf {
    /// Constructs an empty TLSF in place.
    ///
    /// # Safety
    ///
    /// `ptr` must point to writable storage of at least [`control_size`](Self::control_size)
    /// bytes.
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

    /// Returns the registered hook, by value so the caller can drop its heap borrow before calling
    /// it (the hook re-enters through `free`).
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
        self.free_bytes += (*block).size();
    }

    unsafe fn remove_free_block(&mut self, block: *mut BlockHeader) {
        self.free_bytes -= (*block).size();
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

    /// Finds a free block at least as large as class (fl, sl); returns null if none.
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

    /// Marks `block` used, removes it from its free list, and splits any remainder beyond `size`
    /// into a new free block.
    unsafe fn use_block(&mut self, block: *mut BlockHeader, size: usize) {
        self.remove_free_block(block);
        // `split` writes the remainder's header into the poisoned body, then re-poisons the
        // remainder.
        unpoison_block(block);
        self.split(block, size);
        (*block).set_used();
        // tell the (used) physical successor its prev is no longer free
        let next = next_phys(block);
        (*next).set_prev_used();
    }

    /// Splits the remainder beyond `size` into a new free block, if it is large enough to be one.
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

    /// Frees `block`, coalescing it with a free physical predecessor and successor, and registers
    /// the merged block.
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
        // Poison the whole merged body, including absorbed neighbours' headers; the links stay
        // accessible.
        poison_free_body(block);
    }

    /// Adds `[mem, mem + size)` as the heap's pool. `mem` must be `ALIGN`-aligned and must not
    /// overlap the control block.
    ///
    /// # Safety
    ///
    /// The region must be writable and owned by the heap for its lifetime.
    pub unsafe fn add_pool(&mut self, mem: *mut u8, size: usize) {
        debug_assert!((mem as usize).is_multiple_of(ALIGN));
        // Layout: [free block header][... payload ...][sentinel header]
        // The trailing zero-size used sentinel terminates coalescing.
        let pool_overhead = 2 * HEADER_SIZE;
        if size < pool_overhead + MIN_BLOCK_SIZE {
            return;
        }
        let block = mem as *mut BlockHeader;
        // Round down to `ALIGN`, not merely to the flag granularity (`SIZE_MASK` only gives a
        // multiple of 4). The sentinel sits at `block + HEADER_SIZE + block_size`, so a size that
        // is not a multiple of 16 misaligns it, which is UB and fails `check`. A region whose end
        // is unaligned (the device's frunk heap, bounded by linker symbols) yields such a size.
        // The discarded bytes, at most `ALIGN - 1`, were unusable anyway.
        let block_size = (size - pool_overhead) & !(ALIGN - 1);
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
        // Record the pool span for the integrity walk (single pool per heap).
        self.pool_first = block;
        self.pool_end = mem as usize + size;
        self.min_free_bytes = self.free_bytes;
        // The whole pool starts free: poison it so accesses to uncarved space trap too.
        poison_free_body(block);
    }

    /// Walks the pool and returns whether it is intact.
    ///
    /// Checks each block's payload alignment and size granularity, the prev-free flag against the
    /// `prev_phys` back-link, that no two free blocks are adjacent, and that the chain ends at the
    /// zero-size used sentinel. Every step is bounded by `pool_end`, so a corrupted size cannot
    /// walk out of the arena. Reads only headers, so it is ASan-safe; never allocates.
    pub unsafe fn check(&self) -> bool {
        if self.pool_first.is_null() {
            return true; // no pool registered: vacuously intact
        }
        let lo = self.pool_first as usize;
        let mut block = self.pool_first;
        let mut prev: *mut BlockHeader = ptr::null_mut();
        let mut prev_free = false;
        loop {
            let baddr = block as usize;
            // The header itself must sit within the pool.
            if baddr < lo || baddr + HEADER_SIZE > self.pool_end {
                return false;
            }
            // Payloads are 16-aligned by construction.
            if !(payload_of(block) as usize).is_multiple_of(ALIGN) {
                return false;
            }
            // The PREV_FREE flag must agree with the real predecessor, and when set
            // the back-link must point at it.
            if (*block).is_prev_free() != prev_free {
                return false;
            }
            if prev_free && (*block).prev_phys != prev {
                return false;
            }
            let size = (*block).size();
            if size == 0 {
                // Zero-size used sentinel terminates the pool.
                return !(*block).is_free();
            }
            // A real block: aligned size, and its end (== next_phys) must leave room
            // for at least the successor header within the pool.
            if !size.is_multiple_of(ALIGN) {
                return false;
            }
            let end = payload_of(block) as usize + size;
            if end <= baddr || end + HEADER_SIZE > self.pool_end {
                return false;
            }
            // TLSF never leaves two free blocks physically adjacent.
            if prev_free && (*block).is_free() {
                return false;
            }
            prev_free = (*block).is_free();
            prev = block;
            block = next_phys(block);
        }
    }

    /// Rounds a request up to the granularity and the free-block minimum.
    #[inline]
    fn adjust_size(size: usize) -> usize {
        let s = (size + ALIGN - 1) & !(ALIGN - 1);
        if s < MIN_BLOCK_SIZE {
            MIN_BLOCK_SIZE
        } else {
            s
        }
    }

    /// Allocates `size` bytes aligned to `align` (at least `ALIGN`). Returns null if `size` is 0 or
    /// nothing fits; does not reclaim.
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
            self.note_low_water();
            return payload_of(block);
        }

        // Over-aligned: reserve slack to shift the payload to an aligned boundary. A non-zero
        // leading gap must become a valid free block (header plus `MIN_BLOCK_SIZE`), hence
        // `align + HEADER_SIZE + MIN_BLOCK_SIZE`. The searched block's neighbours are both used, so
        // the leading remnant cannot coalesce and is simply inserted.
        let extra = align + HEADER_SIZE + MIN_BLOCK_SIZE;
        let (fl, sl) = mapping_search(adjust + extra);
        let block = self.search_suitable(fl, sl);
        if block.is_null() {
            return ptr::null_mut();
        }
        self.remove_free_block(block);
        // The carve writes headers across the body; the remnant and tail are re-poisoned as they
        // become free.
        unpoison_block(block);

        let payload = payload_of(block) as usize;
        let mut aligned = (payload + align - 1) & !(align - 1);
        let mut gap = aligned - payload;
        // A non-zero gap too small for a leading block pushes the payload up one more step.
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
        self.note_low_water();
        payload_of(block)
    }

    #[inline]
    fn note_low_water(&mut self) {
        self.min_free_bytes = self.min_free_bytes.min(self.free_bytes);
    }

    /// The pool's span, its free bytes and their low-water mark; all zero before `add_pool`.
    pub fn stats(&self) -> Stats {
        if self.pool_first.is_null() {
            return Stats::default();
        }
        Stats {
            pool_bytes: self.pool_end - self.pool_first as usize,
            free_bytes: self.free_bytes,
            min_free_bytes: self.min_free_bytes,
        }
    }

    /// Frees `payload`; null is a no-op.
    pub unsafe fn free(&mut self, payload: *mut u8) {
        if payload.is_null() {
            return;
        }
        let block = block_of(payload);
        self.free_block(block);
    }

    /// Returns the payload size of `payload`'s block; 0 for null.
    pub unsafe fn usable_size(&self, payload: *mut u8) -> usize {
        if payload.is_null() {
            return 0;
        }
        (*block_of(payload)).size()
    }

    /// Shrinks used `block` to `want` payload bytes, freeing the tail if it is large enough to be
    /// its own block.
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

    /// Shrinks `payload`'s block in place if `new_size` fits and the alignment already holds.
    /// Returns whether it did, and the block's payload size before the call (what a moving realloc
    /// must copy).
    pub unsafe fn try_shrink_in_place(
        &mut self,
        payload: *mut u8,
        new_size: usize,
        align: usize,
    ) -> (bool, usize) {
        let block = block_of(payload);
        let cur = (*block).size();
        let want = Self::adjust_size(new_size);
        let aligned_ok = (payload as usize) & (align.max(ALIGN) - 1) == 0;
        if aligned_ok && want <= cur {
            self.shrink_in_place(block, want);
            return (true, cur);
        }
        (false, cur)
    }

    /// Resizes, preserving the leading `min(old, new)` bytes.
    ///
    /// A shrink is in place and pointer-stable; the freed tail still coalesces. A grow moves
    /// (alloc, copy, free). On failure returns null and leaves `payload` intact.
    pub unsafe fn realloc(&mut self, payload: *mut u8, new_size: usize, align: usize) -> *mut u8 {
        if payload.is_null() {
            return self.malloc(new_size, align);
        }
        if new_size == 0 {
            self.free(payload);
            return ptr::null_mut();
        }
        // Shrink in place (pointer-stable). Growth falls through to a move.
        let (shrunk, cur) = self.try_shrink_in_place(payload, new_size, align);
        if shrunk {
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
