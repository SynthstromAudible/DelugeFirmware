//! A level's storage: a fixed array of pointers to chunks of `1 << CHUNK_LOG2` entries, each allocated on demand in
//! thread mode and published with `Release`. Folds and reads load with `Acquire` and treat null as absent.

use crate::CHUNK_LOG2;
use crate::mem;
use core::ptr::NonNull;
use core::sync::atomic::{AtomicPtr, Ordering};

const CHUNK: usize = 1 << CHUNK_LOG2;

/// The chunk pointers of one level's values or masks.
pub(crate) struct Directory<T> {
    slots: NonNull<AtomicPtr<T>>,
    len: usize,
}

impl<T> Directory<T> {
    /// A directory with room for `entries` entries and no chunks allocated. Thread mode.
    pub(crate) fn new(entries: u64) -> Option<Self> {
        let len = usize::try_from(entries.div_ceil(CHUNK as u64)).ok()?.max(1);
        Some(Self {
            slots: mem::alloc_zeroed::<AtomicPtr<T>>(len)?,
            len,
        })
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

    /// Allocates the chunks holding entries `[first, end)`. Thread mode. False on allocation failure or an entry
    /// past the directory.
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
    pub(crate) fn free_all(&self) {
        for c in 0..self.len {
            if let Some(p) = self
                .slot(c)
                .and_then(|s| NonNull::new(s.load(Ordering::Acquire)))
            {
                mem::free(p);
            }
        }
        mem::free(self.slots);
    }
}
