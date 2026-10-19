//! An index of recently searched directories' names, so a lookup in a large directory reads one
//! entry instead of scanning from the start.
//!
//! Each indexed directory maps the [`fold_hash`](crate::dir_entry::fold_hash) of every entry's long
//! and short name to the entry's offset. The index is only ever a hint: a hit is read back and
//! compared by name before it is used, and a name the index does not have is looked up by a full
//! scan. So it can go stale (a file created, removed or renamed) without ever answering wrongly,
//! and needs no invalidation; a stale directory is re-indexed by the scan its next miss makes.
//!
//! Bounded: a few directories, and [`MAX_KEYS`] names across them, least recently used dropped
//! first. A directory with more names than that is not indexed.
use alloc::vec::Vec;

/// Most directories indexed at once.
const MAX_DIRS: usize = 8;

/// Most names indexed across every directory: two per entry (long and short), 8 bytes each.
pub(crate) const MAX_KEYS: usize = 4096;

/// Most candidates one lookup verifies; a name whose hash more entries share is scanned for.
pub(crate) const MAX_CANDIDATES: usize = 4;

/// An indexed directory: its key, its names' (hash, entry offset / 32), sorted, and when it was
/// last used.
struct Slot {
    dir: u32,
    keys: Vec<(u32, u32)>,
    used: u32,
}

/// The index; see the module doc.
pub(crate) struct DirIndex {
    slots: Vec<Slot>,
    clock: u32,
}

impl DirIndex {
    pub(crate) const fn new() -> Self {
        Self {
            slots: Vec::new(),
            clock: 0,
        }
    }

    /// The offsets, in 32-byte entries, of `dir`'s entries whose name hashes to `hash`; empty if
    /// `dir` is not indexed.
    pub(crate) fn candidates(&mut self, dir: u32, hash: u32) -> heapless_offsets::Offsets {
        let mut out = heapless_offsets::Offsets::default();
        self.clock = self.clock.wrapping_add(1);
        let clock = self.clock;
        if let Some(slot) = self.slots.iter_mut().find(|s| s.dir == dir) {
            slot.used = clock;
            let first = slot.keys.partition_point(|&(h, _)| h < hash);
            for &(h, offset) in &slot.keys[first..] {
                if h != hash || !out.push(offset) {
                    break;
                }
            }
        }
        out
    }

    /// Indexes `dir` with `keys`, replacing what it had and dropping the least recently used
    /// directories until the names fit.
    pub(crate) fn store(&mut self, dir: u32, mut keys: Vec<(u32, u32)>) {
        self.slots.retain(|s| s.dir != dir);
        if keys.len() > MAX_KEYS {
            return;
        }
        keys.sort_unstable();
        let mut total: usize = self.slots.iter().map(|s| s.keys.len()).sum();
        while !self.slots.is_empty()
            && (self.slots.len() >= MAX_DIRS || total + keys.len() > MAX_KEYS)
        {
            let oldest = (0..self.slots.len())
                .min_by_key(|&i| self.slots[i].used)
                .unwrap_or(0);
            total -= self.slots.swap_remove(oldest).keys.len();
        }
        if self.slots.try_reserve(1).is_err() {
            return;
        }
        self.clock = self.clock.wrapping_add(1);
        self.slots.push(Slot {
            dir,
            keys,
            used: self.clock,
        });
    }
}

/// A fixed-capacity list of candidate offsets, so a lookup allocates nothing.
pub(crate) mod heapless_offsets {
    use super::MAX_CANDIDATES;

    #[derive(Default)]
    pub(crate) struct Offsets {
        offsets: [u32; MAX_CANDIDATES],
        len: usize,
    }

    impl Offsets {
        /// Adds `offset`; false once full.
        pub(crate) fn push(&mut self, offset: u32) -> bool {
            if self.len == MAX_CANDIDATES {
                return false;
            }
            self.offsets[self.len] = offset;
            self.len += 1;
            true
        }

        pub(crate) fn iter(&self) -> impl Iterator<Item = u32> + '_ {
            self.offsets[..self.len].iter().copied()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;

    #[test]
    fn candidates_are_the_offsets_sharing_the_hash() {
        let mut index = DirIndex::new();
        index.store(7, vec![(30, 1), (10, 2), (30, 5), (20, 3)]);
        assert_eq!(
            index.candidates(7, 30).iter().collect::<Vec<_>>(),
            vec![1, 5]
        );
        assert_eq!(index.candidates(7, 25).iter().count(), 0);
        assert_eq!(index.candidates(8, 30).iter().count(), 0);
    }

    #[test]
    fn storing_a_directory_again_replaces_it() {
        let mut index = DirIndex::new();
        index.store(7, vec![(1, 1)]);
        index.store(7, vec![(2, 9)]);
        assert_eq!(index.candidates(7, 1).iter().count(), 0);
        assert_eq!(index.candidates(7, 2).iter().collect::<Vec<_>>(), vec![9]);
    }

    #[test]
    fn the_least_recently_used_directory_makes_room() {
        let mut index = DirIndex::new();
        let half = MAX_KEYS / 2;
        index.store(1, (0..half as u32).map(|i| (i, i)).collect());
        index.store(2, (0..half as u32).map(|i| (i, i)).collect());
        index.candidates(1, 0); // 1 is now the more recent
        index.store(3, vec![(5, 5)]);
        assert_eq!(
            index.candidates(2, 0).iter().count(),
            0,
            "2 was not dropped"
        );
        assert_eq!(index.candidates(1, 0).iter().count(), 1);
        assert_eq!(index.candidates(3, 5).iter().count(), 1);
    }

    #[test]
    fn a_directory_with_too_many_names_is_not_indexed() {
        let mut index = DirIndex::new();
        index.store(1, (0..=MAX_KEYS as u32).map(|i| (i, i)).collect());
        assert_eq!(index.candidates(1, 0).iter().count(), 0);
    }
}
