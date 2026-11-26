//! The pyramid: one peak per 256 frames at level 0, and each level up one peak per 16 of the level below.
//!
//! Every value is one encoded [`Peak`] in an `AtomicU32`, folded with a compare-and-swap loop that only lowers the
//! minimum and raises the maximum, so the order and interleaving of writers cannot change the result. A level-0 block
//! is complete when its bit is set in its level-1 parent's mask; a level-k entry (k ≥ 1) is complete when its own mask
//! is full. A child's value is folded into its parent before its bit is set (`Release`), and a reader loads a mask
//! with `Acquire` before the value, so a complete entry's value holds every child.

use crate::dir::Directory;
use crate::peak::{Peak, peak_of_frames};
use crate::{BLOCK_FRAMES_LOG2, CHUNK_LOG2, FAN_LOG2, MAX_LEVELS, div, mem};
use core::ptr::NonNull;
use core::sync::atomic::{AtomicBool, AtomicPtr, AtomicU16, AtomicU32, AtomicU64, Ordering, fence};

/// The frames one entry of `level` covers, as a power of two.
pub(crate) const fn shift(level: u32) -> u32 {
    BLOCK_FRAMES_LOG2 + FAN_LOG2 * level
}

/// How many entries of `level` cover `frames` frames.
pub(crate) const fn entries(level: u32, frames: u64) -> u64 {
    let s = shift(level);
    (frames >> s) + if frames & ((1 << s) - 1) != 0 { 1 } else { 0 }
}

/// Folds `e` into `cell`: the per-half unsigned maximum, in a compare-and-swap loop.
pub(crate) fn fold_into(cell: &AtomicU32, e: u32) {
    let mut cur = cell.load(Ordering::Relaxed);
    loop {
        let next = Peak::encode_fold(cur, e);
        if next == cur {
            return;
        }
        match cell.compare_exchange_weak(cur, next, Ordering::Release, Ordering::Relaxed) {
            Ok(_) => return,
            Err(seen) => cur = seen,
        }
    }
}

/// How long a read's window keeps its level-0 chunks, in ms: reads of two views alternating within it keep both.
pub const KEEP_MS: u32 = 1000;

/// Indexes released to zero references, waiting for [`PeakIndex::collect`].
static FREE_LIST: AtomicPtr<PeakIndex> = AtomicPtr::new(core::ptr::null_mut());

/// A sample's waveform peak index.
pub struct PeakIndex {
    refs: AtomicU32,
    frames: AtomicU64,
    is_final: AtomicBool,
    levels: u32,
    values: [Option<Directory<AtomicU32>>; MAX_LEVELS as usize],
    masks: [Option<Directory<AtomicU16>>; MAX_LEVELS as usize],
    range: AtomicU32,
    /// How many level-1 entries are complete: what a sidecar would hold.
    complete1: AtomicU32,
    /// Folds writing a level-0 value right now: a retired level-0 chunk is freed only when this reads 0.
    folding: AtomicU32,
    /// The frames the current keep window covers, `[keep_first, keep_end)`: every window read since `keep_since`,
    /// which is at most [`KEEP_MS`] ago while it grows.
    keep_first: AtomicU64,
    keep_end: AtomicU64,
    /// When the current keep window began, in the caller's ms.
    keep_since: AtomicU32,
    /// The window before it, `[prev_first, prev_end)`: the reads up to `keep_since`.
    prev_first: AtomicU64,
    prev_end: AtomicU64,
    /// When a window was last read.
    keep_ms: AtomicU32,
    next_free: AtomicPtr<PeakIndex>,
    /// A peak every block holds, set by `fill_uniform`: a fixture's sample with no frames behind it.
    #[cfg(feature = "test_support")]
    uniform: AtomicU32,
}

impl PeakIndex {
    /// A new index with one reference. A file of known length (`growing == false`) holds `frames` frames and has every
    /// level-1-and-up chunk allocated now; a take (`growing == true`) starts at `frames` with room for
    /// `capacity_frames`, and grows its chunks through [`PeakIndex::reserve`]. Thread mode. `None` on allocation
    /// failure.
    #[must_use]
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub fn create(frames: u64, growing: bool, capacity_frames: u64) -> Option<NonNull<PeakIndex>> {
        let capacity = capacity_frames.max(frames).max(1);
        let mut levels = 2;
        while levels < MAX_LEVELS && capacity > 1 << shift(levels - 1) {
            levels += 1;
        }
        let mut index = PeakIndex {
            refs: AtomicU32::new(1),
            frames: AtomicU64::new(frames),
            is_final: AtomicBool::new(!growing),
            levels,
            values: [const { None }; MAX_LEVELS as usize],
            masks: [const { None }; MAX_LEVELS as usize],
            range: AtomicU32::new(0),
            complete1: AtomicU32::new(0),
            folding: AtomicU32::new(0),
            keep_first: AtomicU64::new(0),
            keep_end: AtomicU64::new(0),
            keep_since: AtomicU32::new(0),
            prev_first: AtomicU64::new(0),
            prev_end: AtomicU64::new(0),
            keep_ms: AtomicU32::new(0),
            next_free: AtomicPtr::new(core::ptr::null_mut()),
            #[cfg(feature = "test_support")]
            uniform: AtomicU32::new(0),
        };
        let mut ok = true;
        for k in 0..levels {
            let n = entries(k, capacity);
            index.values[k as usize] = if k == 0 {
                Directory::new_retirable(n)
            } else {
                Directory::new(n)
            };
            ok &= index.values[k as usize].is_some();
            if k >= 1 {
                index.masks[k as usize] = Directory::new(n);
                ok &= index.masks[k as usize].is_some();
            }
        }
        if ok && !growing {
            ok = index.reserve_upto(frames);
        }
        if !ok {
            index.free_storage();
            return None;
        }
        // Moved into place only once whole: nothing reaches it before it is returned.
        mem::alloc_init(index)
            .map_err(|index| index.free_storage())
            .ok()
    }

    /// Allocates the level-1-and-up chunks covering `[0, frames)`. Thread mode.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    fn reserve_upto(&self, frames: u64) -> bool {
        let mut ok = true;
        for k in 1..self.levels {
            let n = entries(k, frames);
            ok &= self.values[k as usize]
                .as_ref()
                .is_some_and(|d| d.ensure(0, n));
            ok &= self.masks[k as usize]
                .as_ref()
                .is_some_and(|d| d.ensure(0, n));
        }
        ok
    }

    /// Grows a take's level-1-and-up chunks to cover `frames` frames. Thread mode. A fold past the reserved chunks is
    /// dropped, and its block stays incomplete.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub fn reserve(&self, frames: u64) {
        let _ = self.reserve_upto(frames);
    }

    /// Keeps level-0 values for frames `[first, end)` from now on. Thread mode.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub fn ensure_level0(&self, first: u64, end: u64) {
        if let Some(d) = &self.values[0] {
            let _ = d.ensure(first >> BLOCK_FRAMES_LOG2, entries(0, end));
        }
    }

    /// The frames the index holds now.
    #[must_use]
    pub fn frames(&self) -> u64 {
        self.frames.load(Ordering::Acquire)
    }

    /// SeqCst, against `set_frames`' store: a fold that sets an end entry's last bit and a `set_frames` that makes the
    /// entry need fewer bits each see the other's write, so one of them propagates it.
    fn final_frames(&self) -> Option<u64> {
        self.is_final.load(Ordering::SeqCst).then(|| self.frames())
    }

    /// The bits of `(level, entry)`'s mask that make it complete: all 16 while the index grows; once it is final, the
    /// bits of the children that start before its end.
    fn full_mask(&self, level: u32, entry: u64) -> u16 {
        let Some(frames) = self.final_frames() else {
            return u16::MAX;
        };
        let first_child = entry << FAN_LOG2;
        let children = entries(level - 1, frames)
            .saturating_sub(first_child)
            .min(1 << FAN_LOG2);
        if children >= 1 << FAN_LOG2 {
            u16::MAX
        } else {
            ((1u32 << children) - 1) as u16
        }
    }

    /// The peak `fill_uniform` gave every block, if it was called.
    #[cfg(feature = "test_support")]
    fn uniform(&self) -> Option<Peak> {
        let p = Peak::decode(self.uniform.load(Ordering::Acquire));
        (!p.is_empty()).then_some(p)
    }

    #[cfg(not(feature = "test_support"))]
    #[allow(clippy::unused_self)]
    const fn uniform(&self) -> Option<Peak> {
        None
    }

    /// Whether level-0 block `block`'s bit is set in its parent's mask.
    fn complete_bit(&self, block: u64) -> bool {
        self.masks[1]
            .as_ref()
            .and_then(|d| d.get(block >> FAN_LOG2))
            .is_some_and(|m| m.load(Ordering::Acquire) & (1 << (block & 15)) != 0)
    }

    /// Whether `(level, entry)` is complete.
    pub(crate) fn complete(&self, level: u32, entry: u64) -> bool {
        if self.uniform().is_some() {
            return true;
        }
        if level == 0 {
            return self.complete_bit(entry);
        }
        self.masks[level as usize]
            .as_ref()
            .and_then(|d| d.get(entry))
            .is_some_and(|m| m.load(Ordering::Acquire) == self.full_mask(level, entry))
    }

    /// The stored value of `(level, entry)`, if its chunk exists and holds something.
    pub(crate) fn value(&self, level: u32, entry: u64) -> Option<Peak> {
        if let Some(u) = self.uniform() {
            return Some(u);
        }
        let v = self.values[level as usize]
            .as_ref()?
            .get(entry)?
            .load(Ordering::Relaxed);
        let p = Peak::decode(v);
        (!p.is_empty()).then_some(p)
    }

    /// Completes level-0 block `block` with `peak`: keeps the value if its chunk exists, folds it into the range and
    /// into its parent, and sets its bit. Never allocates. A block already complete only fills its kept value (a
    /// column read's frames, kept for the next read); the range and the levels above keep what its first fold gave
    /// them. Every producer folds the same converted frames, so the two agree; a block folded with different peaks
    /// would leave its kept value wider than its parent's. A block past a
    /// final index's end is dropped.
    pub fn fold_block(&self, block: u64, peak: Peak) {
        if peak.is_empty() {
            return;
        }
        if let Some(f) = self.final_frames()
            && block >= entries(0, f)
        {
            return;
        }
        let e = peak.encode();
        // Counted, so a trim frees a retired chunk only once no fold that could have loaded it is in flight. The fence
        // pairs with the trim's: either this load sees the chunk retired, or the trim sees this fold counted.
        self.folding.fetch_add(1, Ordering::Relaxed);
        fence(Ordering::SeqCst);
        if let Some(v) = self.values[0].as_ref().and_then(|d| d.get(block)) {
            fold_into(v, e);
        }
        self.folding.fetch_sub(1, Ordering::Release);
        if self.complete_bit(block) {
            return;
        }
        fold_into(&self.range, e);
        self.complete_child(1, block >> FAN_LOG2, (block & 15) as u32, e);
    }

    /// Folds `e` into `(level, entry)` and sets child `bit`'s mask bit; propagates up when that made it complete.
    fn complete_child(&self, level: u32, entry: u64, bit: u32, e: u32) {
        let (Some(value), Some(mask)) = (
            self.values[level as usize]
                .as_ref()
                .and_then(|d| d.get(entry)),
            self.masks[level as usize]
                .as_ref()
                .and_then(|d| d.get(entry)),
        ) else {
            return;
        };
        fold_into(value, e);
        let b = 1u16 << bit;
        let old = mask.fetch_or(b, Ordering::SeqCst); // SeqCst: see `final_frames`
        let full = self.full_mask(level, entry);
        let became = old != full && (old | b) == full;
        if became && level == 1 {
            self.complete1.fetch_add(1, Ordering::Relaxed);
        }
        if became && level + 1 < self.levels {
            let v = value.load(Ordering::Acquire);
            self.complete_child(level + 1, entry >> FAN_LOG2, (entry & 15) as u32, v);
        }
    }

    /// Folds native frames starting at `first_frame`, completing every level-0 block wholly inside them (to the file's
    /// end, once the index is final). Partial blocks are left as they are. Never allocates.
    pub fn fold_frames(&self, first_frame: u64, bytes: &[u8], byte_depth: u8, channels: u8) {
        let Some(stride) = byte_depth.checked_mul(channels) else {
            return;
        };
        let Ok(len) = u32::try_from(bytes.len()) else {
            return;
        };
        let n = u64::from(div::frames_in_bytes(len, stride));
        let end = first_frame.saturating_add(n);
        let file_end = self.final_frames();
        let block_frames = 1u64 << BLOCK_FRAMES_LOG2;
        let mut b = entries(0, first_frame);
        loop {
            let start = b << BLOCK_FRAMES_LOG2;
            let mut stop = start + block_frames;
            if let Some(f) = file_end {
                if start >= f {
                    return;
                }
                stop = stop.min(f);
            }
            if stop > end {
                return;
            }
            let from = ((start - first_frame) * u64::from(stride)) as usize;
            let to = ((stop - first_frame) * u64::from(stride)) as usize;
            if let Some(slice) = bytes.get(from..to) {
                self.fold_block(b, peak_of_frames(slice, byte_depth));
            }
            b += 1;
        }
    }

    /// Sets a take's frame count; `is_final` once its length is known, after which the entries at its end complete
    /// with fewer children. Thread mode.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub fn set_frames(&self, frames: u64, is_final: bool) {
        self.frames.store(frames, Ordering::Release);
        if !is_final {
            return;
        }
        self.is_final.store(true, Ordering::SeqCst); // SeqCst: see `final_frames`
        // The last entry of each level may now need fewer children; complete it, bottom-up, if it has them all.
        for level in 1..self.levels {
            let n = entries(level, frames);
            if n == 0 {
                continue;
            }
            let last = n - 1;
            let Some(mask) = self.masks[level as usize]
                .as_ref()
                .and_then(|d| d.get(last))
            else {
                continue;
            };
            let full = self.full_mask(level, last);
            let m = mask.load(Ordering::SeqCst);
            // A last entry with all 16 children was counted when its last child completed it; one with fewer becomes
            // complete only now.
            if level == 1 && m == full && full != u16::MAX {
                self.complete1.fetch_add(1, Ordering::Relaxed);
            }
            if m == full
                && level + 1 < self.levels
                && let Some(v) = self.values[level as usize]
                    .as_ref()
                    .and_then(|d| d.get(last))
            {
                let e = v.load(Ordering::Acquire);
                self.complete_child(level + 1, last >> FAN_LOG2, (last & 15) as u32, e);
            }
        }
    }

    /// How many level-1 entries are complete.
    #[must_use]
    pub fn complete_level1(&self) -> u64 {
        u64::from(self.complete1.load(Ordering::Relaxed))
    }

    /// How many level-1 entries the index has: one per 4,096 frames.
    #[must_use]
    pub fn level1_entries(&self) -> u64 {
        entries(1, self.frames())
    }

    /// The word a sidecar stores for level-1 entry `entry`: its value if complete, else 0.
    pub(crate) fn level1_word(&self, entry: u64) -> u32 {
        if !self.complete(1, entry) {
            return 0;
        }
        self.values[1]
            .as_ref()
            .and_then(|d| d.get(entry))
            .map_or(0, |v| v.load(Ordering::Acquire))
    }

    /// Whether the index is final with `frames` frames: what a sidecar's peaks may be restored into.
    pub(crate) fn is_final_at(&self, frames: u64) -> bool {
        self.final_frames() == Some(frames)
    }

    /// Restores level-1 entry `entry` from a sidecar's word `e`: folds it in, sets its mask full, and propagates it
    /// as a scan completing it would. Never allocates.
    pub(crate) fn restore_level1(&self, entry: u64, e: u32) {
        if e == 0 {
            return;
        }
        let (Some(value), Some(mask)) = (
            self.values[1].as_ref().and_then(|d| d.get(entry)),
            self.masks[1].as_ref().and_then(|d| d.get(entry)),
        ) else {
            return;
        };
        fold_into(value, e);
        let full = self.full_mask(1, entry);
        let old = mask.fetch_or(full, Ordering::SeqCst); // SeqCst: see `final_frames`
        if old == full {
            return; // already complete: its parent has it
        }
        self.complete1.fetch_add(1, Ordering::Relaxed);
        fold_into(&self.range, e);
        if 2 < self.levels {
            let v = value.load(Ordering::Acquire);
            self.complete_child(2, entry >> FAN_LOG2, (entry & 15) as u32, v);
        }
    }

    /// Keeps level 0 for frames `[first, end)`. Reads within [`KEEP_MS`] of the current window's start widen it; a
    /// later one starts a new window, and the current one becomes the previous. So a trim keeps every window read in
    /// the last [`KEEP_MS`], and a view scrolling steadily holds at most two windows' worth. Thread mode.
    pub fn keep_level0(&self, first: u64, end: u64, now_ms: u32) {
        let (kf, ke) = (
            self.keep_first.load(Ordering::Relaxed),
            self.keep_end.load(Ordering::Relaxed),
        );
        let growing =
            ke > kf && now_ms.wrapping_sub(self.keep_since.load(Ordering::Relaxed)) <= KEEP_MS;
        if growing {
            self.keep_first.store(first.min(kf), Ordering::Relaxed);
            self.keep_end.store(end.max(ke), Ordering::Relaxed);
        } else {
            self.prev_first.store(kf, Ordering::Relaxed);
            self.prev_end.store(ke, Ordering::Relaxed);
            self.keep_first.store(first, Ordering::Relaxed);
            self.keep_end.store(end, Ordering::Relaxed);
            self.keep_since.store(now_ms, Ordering::Relaxed);
        }
        self.keep_ms.store(now_ms, Ordering::Relaxed);
    }

    /// Frees the level-0 chunks earlier passes retired, if no fold is in flight, then retires those outside the kept
    /// window (every one once the window is older than [`KEEP_MS`]); how many it retired. A chunk retired now is
    /// freed by a later pass. Thread mode.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub fn trim_level0(&self, now_ms: u32) -> u32 {
        let Some(d) = &self.values[0] else {
            return 0;
        };
        // Pairs with the fold's fence: a fold that loaded a chunk retired by an earlier pass is counted here, and one
        // that starts after it sees the chunk gone. Acquire: a finished fold's writes happen before the free.
        fence(Ordering::SeqCst);
        if self.folding.load(Ordering::Acquire) == 0 {
            d.reap();
        }
        let chunk_log2 = BLOCK_FRAMES_LOG2 + CHUNK_LOG2;
        let chunks = |first: u64, end: u64| {
            let f = first >> chunk_log2;
            let e = end.saturating_add((1 << chunk_log2) - 1) >> chunk_log2;
            (
                usize::try_from(f).unwrap_or(usize::MAX),
                usize::try_from(e).unwrap_or(usize::MAX),
            )
        };
        // The current window while it is read; the previous one until KEEP_MS after the current one began, which is
        // when its last read was.
        let current = if now_ms.wrapping_sub(self.keep_ms.load(Ordering::Relaxed)) <= KEEP_MS {
            chunks(
                self.keep_first.load(Ordering::Relaxed),
                self.keep_end.load(Ordering::Relaxed),
            )
        } else {
            (0, 0)
        };
        let previous = if now_ms.wrapping_sub(self.keep_since.load(Ordering::Relaxed)) <= KEEP_MS {
            chunks(
                self.prev_first.load(Ordering::Relaxed),
                self.prev_end.load(Ordering::Relaxed),
            )
        } else {
            (0, 0)
        };
        d.retire_outside(current, previous)
    }

    /// How many level-0 chunks are published.
    #[must_use]
    pub fn level0_chunks(&self) -> u32 {
        self.values[0].as_ref().map_or(0, Directory::published)
    }

    /// The range over every block completed so far, or `None` before the first.
    #[must_use]
    pub fn range(&self) -> Option<Peak> {
        let p = Peak::decode(self.range.load(Ordering::Acquire));
        (!p.is_empty()).then_some(p)
    }

    /// The first frame of the first incomplete level-0 block that ends after frame `from` (so it may start up to 255
    /// frames before it), or `None` if every block from there to the index's frames is complete.
    #[must_use]
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub fn next_unscanned(&self, from: u64) -> Option<u64> {
        let blocks = entries(0, self.frames());
        let first = from >> BLOCK_FRAMES_LOG2;
        if first >= blocks {
            return None;
        }
        let top = self.levels - 1;
        (0..entries(top, self.frames()))
            .find_map(|e| self.find_incomplete(top, e, first, blocks))
            .map(|b| b << BLOCK_FRAMES_LOG2)
    }

    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    fn find_incomplete(&self, level: u32, entry: u64, first: u64, blocks: u64) -> Option<u64> {
        let span = shift(level) - BLOCK_FRAMES_LOG2;
        let (lo, hi) = (entry << span, (entry + 1) << span);
        if hi <= first || lo >= blocks {
            return None;
        }
        if level == 0 {
            return (!self.complete(0, entry)).then_some(entry);
        }
        if self.complete(level, entry) {
            return None;
        }
        ((entry << FAN_LOG2)..((entry + 1) << FAN_LOG2))
            .find_map(|c| self.find_incomplete(level - 1, c, first, blocks))
    }

    /// The number of levels.
    pub(crate) fn levels(&self) -> u32 {
        self.levels
    }

    /// Takes a reference. Any tier.
    pub fn retain(&self) {
        self.refs.fetch_add(1, Ordering::Relaxed);
    }

    /// Drops a reference. The last one queues the index for [`PeakIndex::collect`]. Any tier.
    pub fn release(index: NonNull<PeakIndex>) {
        // SAFETY: the caller holds a reference, so the index is alive.
        let this = unsafe { index.as_ref() };
        if this.refs.fetch_sub(1, Ordering::AcqRel) != 1 {
            return;
        }
        let mut head = FREE_LIST.load(Ordering::Relaxed);
        loop {
            this.next_free.store(head, Ordering::Relaxed);
            match FREE_LIST.compare_exchange_weak(
                head,
                index.as_ptr(),
                Ordering::Release,
                Ordering::Relaxed,
            ) {
                Ok(_) => return,
                Err(seen) => head = seen,
            }
        }
    }

    /// Frees every index whose last reference has gone. Thread mode.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub fn collect() {
        let mut p = FREE_LIST.swap(core::ptr::null_mut(), Ordering::Acquire);
        while let Some(index) = NonNull::new(p) {
            // SAFETY: an index on the free list has no references left, so nothing else reaches it.
            let this = unsafe { index.as_ref() };
            p = this.next_free.load(Ordering::Relaxed);
            this.free_storage();
            mem::free(index);
        }
    }

    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    fn free_storage(&self) {
        for d in self.values.iter().flatten() {
            d.free_all();
        }
        for d in self.masks.iter().flatten() {
            d.free_all();
        }
    }

    /// The word a sidecar stores for level-1 entry `entry`.
    #[cfg(feature = "test_support")]
    #[must_use]
    pub fn level1_word_for_test(&self, entry: u64) -> u32 {
        self.level1_word(entry)
    }

    /// Whether level-0 block `block` is complete. Test support.
    #[cfg(feature = "test_support")]
    #[doc(hidden)]
    #[must_use]
    pub fn block_complete(&self, block: u64) -> bool {
        self.complete(0, block)
    }

    /// Level-0 block `block`'s stored value. Test support.
    #[cfg(feature = "test_support")]
    #[doc(hidden)]
    #[must_use]
    pub fn block_peak(&self, block: u64) -> Option<Peak> {
        self.value(0, block)
    }

    /// Widens the range by `peak` without completing any block. Test support: a fixture drawn at a scale its peaks do
    /// not reach.
    #[cfg(feature = "test_support")]
    #[doc(hidden)]
    pub fn widen_range(&self, peak: Peak) {
        fold_into(&self.range, peak.encode());
    }

    /// Makes every block of the index complete with `peak`, without storing per-block values. Test support: seeding
    /// a fixture whose sample has no frames behind it.
    #[cfg(feature = "test_support")]
    #[doc(hidden)]
    pub fn fill_uniform(&self, peak: Peak) {
        self.uniform.store(peak.encode(), Ordering::Release);
        fold_into(&self.range, peak.encode());
    }
}
