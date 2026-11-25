//! A column's plan: the stored peaks that tile its span, and at most two short frame ranges at its ends to read.
//!
//! An entry the span covers wholly and that is complete gives its stored value. Only the (at most two) level-1
//! entries holding the span's ends are covered in part; inside each, the level-0 blocks the span covers wholly and
//! that are complete give their stored values, and the rest (a block covered in part, or a complete block whose value
//! is not kept) are read from the frames as one range. So a column reads at most two ranges, together at most 8,192 frames.
//! A block or entry the span covers wholly that is not complete makes the column pending.

use crate::index::{PeakIndex, shift};
use crate::peak::Peak;
use crate::{BLOCK_FRAMES_LOG2, FAN_LOG2};

/// Up to two frame ranges `[lo, hi)` the caller reads and folds in.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct FrameReads {
    /// The ranges; only the first `count` are meaningful.
    pub ranges: [(u64, u64); 2],
    /// How many ranges there are.
    pub count: u8,
}

/// How to answer one column.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ColumnPlan {
    /// A block the span covers wholly is not scanned yet.
    Pending,
    /// The fold of `interior` (if any) and the peaks of `reads` is the column's peak.
    Ready {
        /// The stored peaks' fold, or `None` if the span is covered by reads alone.
        interior: Option<Peak>,
        /// The ranges to read directly.
        reads: FrameReads,
    },
}

/// The column's peak: `plan`'s interior folded with `read(lo, hi)` for each range. `None` if the plan is pending or a
/// read is unavailable (the caller answers pending).
pub fn resolve(plan: ColumnPlan, mut read: impl FnMut(u64, u64) -> Option<Peak>) -> Option<Peak> {
    let ColumnPlan::Ready { interior, reads } = plan else {
        return None;
    };
    let mut acc = interior.unwrap_or(Peak::EMPTY);
    for &(lo, hi) in &reads.ranges[..usize::from(reads.count)] {
        acc = acc.fold(read(lo, hi)?);
    }
    (!acc.is_empty()).then_some(acc)
}

struct Walk {
    first: u64,
    end: u64,
    frames: u64,
    acc: Peak,
    reads: FrameReads,
    pending: bool,
}

impl Walk {
    fn span(&self, level: u32, entry: u64) -> (u64, u64) {
        let s = shift(level);
        let lo = entry << s;
        (lo, ((entry + 1) << s).min(self.frames))
    }

    fn read(&mut self, lo: u64, hi: u64) {
        if hi <= lo {
            return;
        }
        let n = usize::from(self.reads.count);
        if n > 0 && self.reads.ranges[n - 1].1 == lo {
            self.reads.ranges[n - 1].1 = hi;
        } else if n < self.reads.ranges.len() {
            self.reads.ranges[n] = (lo, hi);
            self.reads.count += 1;
        } else {
            // Unreachable by construction (only two level-1 entries are covered in part); answering pending keeps
            // the column honest if it ever were.
            self.pending = true;
        }
    }
}

impl PeakIndex {
    /// The plan for frames `[first, end)`, which the caller has clipped to the index's frames. It neither allocates
    /// nor locks.
    #[must_use]
    pub fn column(&self, first: u64, end: u64) -> ColumnPlan {
        let frames = self.frames();
        let end = end.min(frames);
        if end <= first {
            return ColumnPlan::Ready {
                interior: None,
                reads: FrameReads::default(),
            };
        }
        let mut w = Walk {
            first,
            end,
            frames,
            acc: Peak::EMPTY,
            reads: FrameReads::default(),
            pending: false,
        };
        let top = self.levels() - 1;
        let s = shift(top);
        for e in (first >> s)..=((end - 1) >> s) {
            self.visit(&mut w, top, e);
            if w.pending {
                return ColumnPlan::Pending;
            }
        }
        ColumnPlan::Ready {
            interior: (!w.acc.is_empty()).then_some(w.acc),
            reads: w.reads,
        }
    }

    fn visit(&self, w: &mut Walk, level: u32, entry: u64) {
        let (lo, hi) = w.span(level, entry);
        let (a, b) = (lo.max(w.first), hi.min(w.end));
        if b <= a || w.pending {
            return;
        }
        let whole = a == lo && b == hi;
        if whole
            && self.complete(level, entry)
            && let Some(v) = self.value(level, entry)
        {
            w.acc = w.acc.fold(v);
            return;
        }
        if level == 1 {
            if whole {
                // Wholly covered but not complete: a block under it is not scanned yet.
                w.pending = true;
            } else {
                self.visit_edge(w, entry, a, b);
            }
            return;
        }
        let children = (entry << FAN_LOG2)..((entry + 1) << FAN_LOG2);
        for c in children {
            self.visit(w, level - 1, c);
        }
    }

    /// A level-1 entry the span covers in part, over frames `[a, b)`: fold its wholly covered, complete, valued blocks,
    /// and read the rest as one range from the first block that needs reading to the last.
    fn visit_edge(&self, w: &mut Walk, entry: u64, a: u64, b: u64) {
        let first_block = a >> BLOCK_FRAMES_LOG2;
        let last_block = (b - 1) >> BLOCK_FRAMES_LOG2;
        let mut need: Option<(u64, u64)> = None;
        for block in first_block..=last_block {
            debug_assert_eq!(block >> FAN_LOG2, entry);
            let (lo, hi) = w.span(0, block);
            let whole = lo >= a && hi <= b;
            if whole && !self.complete(0, block) {
                w.pending = true;
                return;
            }
            if !(whole && self.value(0, block).is_some()) {
                need = Some(need.map_or((block, block), |(l, _)| (l, block)));
            }
        }
        for block in first_block..=last_block {
            if need.is_some_and(|(l, h)| (l..=h).contains(&block)) {
                continue;
            }
            if let Some(v) = self.value(0, block) {
                w.acc = w.acc.fold(v);
            }
        }
        if let Some((l, h)) = need {
            let lo = (l << BLOCK_FRAMES_LOG2).max(a);
            let hi = ((h + 1) << BLOCK_FRAMES_LOG2).min(b);
            w.read(lo, hi);
        }
    }
}
