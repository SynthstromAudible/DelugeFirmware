//! The pyramid: one peak per 256 frames at level 0, and each level up one peak per 16 of the level below.
//!
//! Every value is one encoded [`Peak`] in an `AtomicU32`, folded with a compare-and-swap loop that only lowers the
//! minimum and raises the maximum, so the order and interleaving of writers cannot change the result. A level-0 block
//! is complete when its bit is set in its level-1 parent's mask; a level-k entry (k ≥ 1) is complete when its own mask
//! is full. A child's value is folded into its parent before its bit is set (`Release`), and a reader loads a mask
//! with `Acquire` before the value, so a complete entry's value holds every child.

use crate::dir::Directory;
use crate::peak::{Peak, peak_of_frames};
use crate::{BLOCK_FRAMES_LOG2, FAN_LOG2, MAX_LEVELS, div, mem};
use core::ptr::NonNull;
use core::sync::atomic::{AtomicBool, AtomicPtr, AtomicU16, AtomicU32, AtomicU64, Ordering};

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
    pub fn create(frames: u64, growing: bool, capacity_frames: u64) -> Option<NonNull<PeakIndex>> {
        let capacity = capacity_frames.max(frames).max(1);
        let mut levels = 2;
        while levels < MAX_LEVELS && capacity > 1 << shift(levels - 1) {
            levels += 1;
        }
        let p = mem::alloc_zeroed::<PeakIndex>(1)?;
        // SAFETY: `p` is zeroed memory for one `PeakIndex`, which `write` initialises without reading.
        unsafe {
            p.as_ptr().write(PeakIndex {
                refs: AtomicU32::new(1),
                frames: AtomicU64::new(frames),
                is_final: AtomicBool::new(!growing),
                levels,
                values: [const { None }; MAX_LEVELS as usize],
                masks: [const { None }; MAX_LEVELS as usize],
                range: AtomicU32::new(0),
                next_free: AtomicPtr::new(core::ptr::null_mut()),
                #[cfg(feature = "test_support")]
                uniform: AtomicU32::new(0),
            });
        }
        // SAFETY: just initialised, and not shared yet.
        let index = unsafe { &mut *p.as_ptr() };
        let mut ok = true;
        for k in 0..levels {
            let n = entries(k, capacity);
            index.values[k as usize] = Directory::new(n);
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
            mem::free(p);
            return None;
        }
        Some(p)
    }

    /// Allocates the level-1-and-up chunks covering `[0, frames)`. Thread mode.
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
    pub fn reserve(&self, frames: u64) {
        let _ = self.reserve_upto(frames);
    }

    /// Keeps level-0 values for frames `[first, end)` from now on. Thread mode.
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

    fn final_frames(&self) -> Option<u64> {
        self.is_final.load(Ordering::Acquire).then(|| self.frames())
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

    /// Whether `(level, entry)` is complete.
    pub(crate) fn complete(&self, level: u32, entry: u64) -> bool {
        if self.uniform().is_some() {
            return true;
        }
        if level == 0 {
            return self.masks[1]
                .as_ref()
                .and_then(|d| d.get(entry >> FAN_LOG2))
                .is_some_and(|m| m.load(Ordering::Acquire) & (1 << (entry & 15)) != 0);
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
    /// into its parent, and sets its bit. Never allocates. A block already complete is unchanged.
    pub fn fold_block(&self, block: u64, peak: Peak) {
        if peak.is_empty() {
            return;
        }
        let e = peak.encode();
        if let Some(v) = self.values[0].as_ref().and_then(|d| d.get(block)) {
            fold_into(v, e);
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
        let old = mask.fetch_or(b, Ordering::AcqRel);
        let full = self.full_mask(level, entry);
        if old != full && (old | b) == full && level + 1 < self.levels {
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
    pub fn set_frames(&self, frames: u64, is_final: bool) {
        self.frames.store(frames, Ordering::Release);
        if !is_final {
            return;
        }
        self.is_final.store(true, Ordering::Release);
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
            if mask.load(Ordering::Acquire) == self.full_mask(level, last)
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

    /// The range over every block completed so far, or `None` before the first.
    #[must_use]
    pub fn range(&self) -> Option<Peak> {
        let p = Peak::decode(self.range.load(Ordering::Acquire));
        (!p.is_empty()).then_some(p)
    }

    /// The first frame of the first incomplete level-0 block at or after frame `from`, or `None` if every block from
    /// there to the index's frames is complete.
    #[must_use]
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

    fn free_storage(&self) {
        for d in self.values.iter().flatten() {
            d.free_all();
        }
        for d in self.masks.iter().flatten() {
            d.free_all();
        }
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

    /// Makes every block of the index complete with `peak`, without storing per-block values. Test support: seeding
    /// a fixture whose sample has no frames behind it.
    #[cfg(feature = "test_support")]
    #[doc(hidden)]
    pub fn fill_uniform(&self, peak: Peak) {
        self.uniform.store(peak.encode(), Ordering::Release);
        fold_into(&self.range, peak.encode());
    }
}
