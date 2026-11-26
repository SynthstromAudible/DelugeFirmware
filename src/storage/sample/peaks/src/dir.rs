//! A level's storage: a fixed array of pointers to chunks of `1 << CHUNK_LOG2` entries, each allocated on demand in
//! thread mode and published with `Release`. Folds and reads load with `Acquire` and treat null as absent.
//!
//! A retirable directory (level 0's values) can also take chunks back: thread mode unpublishes a chunk into the
//! slot's limbo, and frees it on a later pass once its owner knows no fold that could have loaded it is in flight.

use crate::CHUNK_LOG2;
use crate::mem;
use core::ptr::NonNull;
use core::sync::atomic::{AtomicPtr, Ordering};

const CHUNK: usize = 1 << CHUNK_LOG2;

/// The chunk pointers of one level's values or masks.
pub(crate) struct Directory<T> {
    slots: NonNull<AtomicPtr<T>>,
    len: usize,
    /// Per slot, a chunk unpublished and not yet freed; `None` for a directory that never retires.
    limbo: Option<NonNull<AtomicPtr<T>>>,
}

impl<T> Directory<T> {
    /// A directory with room for `entries` entries and no chunks allocated. Thread mode.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub(crate) fn new(entries: u64) -> Option<Self> {
        let len = usize::try_from(entries.div_ceil(CHUNK as u64)).ok()?.max(1);
        Some(Self {
            slots: mem::alloc_zeroed::<AtomicPtr<T>>(len)?,
            len,
            limbo: None,
        })
    }

    /// As [`Directory::new`], able to retire chunks. Thread mode.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub(crate) fn new_retirable(entries: u64) -> Option<Self> {
        let mut d = Self::new(entries)?;
        match mem::alloc_zeroed::<AtomicPtr<T>>(d.len) {
            Some(limbo) => d.limbo = Some(limbo),
            None => {
                d.free_all();
                return None;
            }
        }
        Some(d)
    }

    fn limbo_slot(&self, chunk: usize) -> Option<&AtomicPtr<T>> {
        let limbo = self.limbo?;
        // SAFETY: `limbo` holds `len` initialised (zeroed) atomics for the directory's life.
        (chunk < self.len).then(|| unsafe { &*limbo.as_ptr().add(chunk) })
    }

    fn slot(&self, chunk: usize) -> Option<&AtomicPtr<T>> {
        // SAFETY: `slots` holds `len` initialised (zeroed) atomics for the directory's life.
        (chunk < self.len).then(|| unsafe { &*self.slots.as_ptr().add(chunk) })
    }

    /// The entry, if its chunk exists.
    pub(crate) fn get(&self, entry: u64) -> Option<&T> {
        let chunk = usize::try_from(entry >> CHUNK_LOG2).ok()?;
        let p = NonNull::new(self.slot(chunk)?.load(Ordering::Acquire))?;
        let within = (entry & (CHUNK as u64 - 1)) as usize;
        // SAFETY: a published chunk holds `CHUNK` zero-initialised `T`s until `free_all`, and `within < CHUNK`.
        Some(unsafe { &*p.as_ptr().add(within) })
    }

    /// Unpublishes every chunk outside chunks `[keep_first, keep_end)` whose slot's limbo is empty, into its limbo;
    /// how many it moved. Thread mode.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub(crate) fn retire_outside(&self, keep_first: usize, keep_end: usize) -> u32 {
        let mut moved = 0;
        for c in (0..self.len).filter(|&c| c < keep_first || c >= keep_end) {
            let (Some(slot), Some(limbo)) = (self.slot(c), self.limbo_slot(c)) else {
                continue;
            };
            if !limbo.load(Ordering::Relaxed).is_null() || slot.load(Ordering::Relaxed).is_null() {
                continue;
            }
            let p = slot.swap(core::ptr::null_mut(), Ordering::SeqCst);
            limbo.store(p, Ordering::Relaxed);
            moved += u32::from(!p.is_null());
        }
        moved
    }

    /// Frees every retired chunk. Thread mode, once no fold that could have loaded one is in flight.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub(crate) fn reap(&self) {
        for c in 0..self.len {
            if let Some(p) = self
                .limbo_slot(c)
                .and_then(|l| NonNull::new(l.swap(core::ptr::null_mut(), Ordering::Relaxed)))
            {
                mem::free(p);
            }
        }
    }

    /// How many slots hold a published chunk.
    pub(crate) fn published(&self) -> u32 {
        (0..self.len)
            .filter(|&c| {
                self.slot(c)
                    .is_some_and(|s| !s.load(Ordering::Relaxed).is_null())
            })
            .count() as u32
    }

    /// Allocates the chunks holding entries `[first, end)`. Thread mode. False on allocation failure or an entry
    /// past the directory.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub(crate) fn ensure(&self, first: u64, end: u64) -> bool {
        if end <= first {
            return true;
        }
        for c in (first >> CHUNK_LOG2)..=((end - 1) >> CHUNK_LOG2) {
            let Some(slot) = usize::try_from(c).ok().and_then(|c| self.slot(c)) else {
                return false;
            };
            if slot.load(Ordering::Acquire).is_null() {
                let Some(chunk) = mem::alloc_zeroed::<T>(CHUNK) else {
                    return false;
                };
                slot.store(chunk.as_ptr(), Ordering::Release);
            }
        }
        true
    }

    /// Frees every chunk and the directory. Thread mode, once no fold or read can reach it.
    #[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_text"))]
    pub(crate) fn free_all(&self) {
        for c in 0..self.len {
            if let Some(p) = self
                .slot(c)
                .and_then(|s| NonNull::new(s.load(Ordering::Acquire)))
            {
                mem::free(p);
            }
        }
        self.reap();
        if let Some(limbo) = self.limbo {
            mem::free(limbo);
        }
        mem::free(self.slots);
    }
}
