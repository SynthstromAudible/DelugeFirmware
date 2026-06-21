//! Host-only randomized invariant harness for the allocator, shared by the unit
//! tests (`tests/`) and the fuzz targets (`fuzz/`). Mirrors the C++ conformance
//! suite (tests/spec_alloc) so the Rust and C++ sides check the same invariants:
//! 16-byte (and requested) alignment, non-overlapping live blocks, per-block data
//! integrity across churn, realloc fidelity, and full reclamation after a drain.
//!
//! Available on the host only (`cfg(any(test, feature = "fuzzing"))`); uses `std`.

use crate::{
    deluge_alloc, deluge_free, deluge_heap_create, deluge_realloc, deluge_usable_size, DelugeHeap,
};

/// One fuzzer/test-driven operation against a slot table.
#[derive(Clone, Copy, Debug)]
pub enum Op {
    Alloc {
        slot: usize,
        size: usize,
        align: usize,
    },
    Free {
        slot: usize,
    },
    Realloc {
        slot: usize,
        size: usize,
        align: usize,
    },
}

struct Live {
    ptr: *mut u8,
    usable: usize,
    seed: u8,
}

/// A heap over an owned arena plus a model of what's live, validating invariants
/// after every operation. Any violation returns `Err(reason)`.
pub struct Harness {
    arena: Vec<u8>,
    h: *mut DelugeHeap,
    slots: Vec<Option<Live>>,
    counter: u8,
}

impl Harness {
    pub fn new(arena_bytes: usize, num_slots: usize) -> Self {
        let mut arena = vec![0u8; arena_bytes];
        let h = unsafe { deluge_heap_create(arena.as_mut_ptr(), arena_bytes) };
        assert!(!h.is_null(), "heap_create failed for {arena_bytes} bytes");
        let mut slots = Vec::with_capacity(num_slots);
        slots.resize_with(num_slots, || None);
        Harness {
            arena,
            h,
            slots,
            counter: 1,
        }
    }

    fn next_seed(&mut self) -> u8 {
        self.counter = self.counter.wrapping_add(1);
        if self.counter == 0 {
            self.counter = 1;
        }
        self.counter
    }

    fn write_pattern(ptr: *mut u8, n: usize, seed: u8) {
        unsafe {
            for i in 0..n {
                *ptr.add(i) = seed.wrapping_add(i as u8);
            }
        }
    }

    fn check_pattern(ptr: *mut u8, n: usize, seed: u8) -> bool {
        unsafe {
            for i in 0..n {
                if *ptr.add(i) != seed.wrapping_add(i as u8) {
                    return false;
                }
            }
        }
        true
    }

    fn overlaps(a: &Live, p: usize, usable: usize) -> bool {
        let a0 = a.ptr as usize;
        let a1 = a0 + a.usable;
        let b0 = p;
        let b1 = p + usable;
        a0 < b1 && b0 < a1
    }

    /// Apply one op, checking all invariants. Returns Err on a violation.
    pub fn step(&mut self, op: Op) -> Result<(), String> {
        let n = self.slots.len();
        match op {
            Op::Alloc { slot, size, align } => {
                let slot = slot % n;
                if self.slots[slot].is_some() {
                    return Ok(()); // occupied; skip (model stays consistent)
                }
                let align = align.max(1).next_power_of_two().min(4096);
                let size = (size % 65536) + 1;
                let p = unsafe { deluge_alloc(self.h, size, align) };
                if p.is_null() {
                    return Ok(()); // out of space is legal
                }
                let need = align.max(16);
                if !(p as usize).is_multiple_of(need) {
                    return Err(format!("misaligned: ptr={p:p} need align {need}"));
                }
                let usable = unsafe { deluge_usable_size(self.h, p) };
                if usable < size {
                    return Err(format!("usable {usable} < requested {size}"));
                }
                for (i, s) in self.slots.iter().enumerate() {
                    if i != slot {
                        if let Some(live) = s {
                            if Self::overlaps(live, p as usize, usable) {
                                return Err(format!("overlap: new {p:p}+{usable} hits slot {i}"));
                            }
                        }
                    }
                }
                let seed = self.next_seed();
                Self::write_pattern(p, usable, seed);
                self.slots[slot] = Some(Live {
                    ptr: p,
                    usable,
                    seed,
                });
            }
            Op::Free { slot } => {
                let slot = slot % n;
                if let Some(live) = self.slots[slot].take() {
                    if !Self::check_pattern(live.ptr, live.usable, live.seed) {
                        return Err(format!("corruption detected freeing slot {slot}"));
                    }
                    unsafe { deluge_free(self.h, live.ptr) };
                }
            }
            Op::Realloc { slot, size, align } => {
                let slot = slot % n;
                let Some(live) = self.slots[slot].take() else {
                    return Ok(());
                };
                if !Self::check_pattern(live.ptr, live.usable, live.seed) {
                    return Err(format!("corruption before realloc of slot {slot}"));
                }
                let align = align.max(1).next_power_of_two().min(4096);
                let new_size = (size % 65536) + 1;
                let np = unsafe { deluge_realloc(self.h, live.ptr, new_size, align) };
                if np.is_null() {
                    // failure must leave the original intact
                    if !Self::check_pattern(live.ptr, live.usable, live.seed) {
                        return Err(format!("failed realloc corrupted original slot {slot}"));
                    }
                    self.slots[slot] = Some(live);
                    return Ok(());
                }
                let preserved = live.usable.min(new_size);
                if !Self::check_pattern(np, preserved, live.seed) {
                    return Err(format!("realloc lost data, slot {slot}"));
                }
                let need = align.max(16);
                if !(np as usize).is_multiple_of(need) {
                    return Err(format!("realloc misaligned slot {slot}"));
                }
                let usable = unsafe { deluge_usable_size(self.h, np) };
                let seed = self.next_seed();
                Self::write_pattern(np, usable, seed);
                self.slots[slot] = Some(Live {
                    ptr: np,
                    usable,
                    seed,
                });
            }
        }
        Ok(())
    }

    /// Free everything still live (checking integrity), then assert the arena
    /// coalesced back: a single large allocation must succeed.
    pub fn drain_and_check(&mut self) -> Result<(), String> {
        for i in 0..self.slots.len() {
            if let Some(live) = self.slots[i].take() {
                if !Self::check_pattern(live.ptr, live.usable, live.seed) {
                    return Err(format!("corruption draining slot {i}"));
                }
                unsafe { deluge_free(self.h, live.ptr) };
            }
        }
        // After a full drain, most of the arena should be one free block again.
        let big = self.arena.len() - self.arena.len() / 8;
        let p = unsafe { deluge_alloc(self.h, big, 16) };
        if p.is_null() {
            return Err(format!(
                "no {big}-byte block after full drain (coalescing leak)"
            ));
        }
        unsafe { deluge_free(self.h, p) };
        Ok(())
    }
}

/// Decode a fuzzer byte string into a bounded op sequence and run it. Returns
/// Err on any invariant violation (the fuzzer treats Err as a crash).
pub fn run_bytes(data: &[u8]) -> Result<(), String> {
    let mut h = Harness::new(1 << 20, 256);
    let mut it = data.iter().copied();
    let mut next = || it.next();
    while let Some(tag) = next() {
        let slot = next().unwrap_or(0) as usize;
        let a = next().unwrap_or(0) as usize;
        let b = next().unwrap_or(0) as usize;
        let size = (a | (b << 8)) + 1;
        let align = 1usize << ((next().unwrap_or(0) % 9) as usize); // 1..256
        let op = match tag % 3 {
            0 => Op::Alloc { slot, size, align },
            1 => Op::Free { slot },
            _ => Op::Realloc { slot, size, align },
        };
        h.step(op)?;
    }
    h.drain_and_check()
}

// ===========================================================================
// Slab cache pool model. Drives the slab + competing general allocations
// and asserts the eviction/pinning invariants after every op:
//   * a PINNED slot is never evicted, and its data is never disturbed;
//   * no two live slots (or general allocs) ever alias;
//   * every live slot/alloc stays aligned, in-range, usable, pattern-intact.
// Eviction is observed through the on_evict callback into a thread-local queue.
// ===========================================================================

use crate::slab::{
    deluge_slab_acquire, deluge_slab_create, deluge_slab_pin, deluge_slab_release,
    deluge_slab_touch, deluge_slab_unpin, DelugeSlab,
};
use core::cell::RefCell;
use core::ffi::c_void;

thread_local! {
    static EVICTED: RefCell<Vec<usize>> = const { RefCell::new(Vec::new()) };
}

unsafe extern "C" fn on_evict(owner: *mut c_void) {
    EVICTED.with(|e| e.borrow_mut().push(owner as usize));
}

#[derive(Clone, Copy, Debug)]
pub enum SlabOp {
    Acquire,
    Pin(usize),
    Unpin(usize),
    Touch(usize),
    Release(usize),
    GeneralAlloc(usize),
    GeneralFree(usize),
}

struct SlabLive {
    ptr: *mut u8,
    owner: usize,
    pinned: bool,
    pattern: u8,
}

struct General {
    ptr: *mut u8,
    size: usize,
    pattern: u8,
}

pub struct SlabModel {
    // Owns the backing memory for the heap's lifetime (read only via lo/hi).
    #[allow(dead_code)]
    arena: Vec<u8>,
    heap: *mut DelugeHeap,
    slab: *mut DelugeSlab,
    slot_size: usize,
    live: Vec<SlabLive>,
    generals: Vec<General>,
    next_owner: usize,
    next_pat: u8,
    lo: usize,
    hi: usize,
}

impl SlabModel {
    pub fn new(arena_bytes: usize, slot_size: usize, capacity: usize) -> Self {
        let mut arena = vec![0u8; arena_bytes];
        let lo = arena.as_ptr() as usize;
        let hi = lo + arena_bytes;
        let heap = unsafe { deluge_heap_create(arena.as_mut_ptr(), arena_bytes) };
        assert!(!heap.is_null());
        EVICTED.with(|e| e.borrow_mut().clear());
        let slab = unsafe { deluge_slab_create(heap, slot_size, capacity, Some(on_evict)) };
        assert!(!slab.is_null());
        SlabModel {
            arena,
            heap,
            slab,
            slot_size,
            live: Vec::new(),
            generals: Vec::new(),
            next_owner: 1,
            next_pat: 1,
            lo,
            hi,
        }
    }

    fn pat(&mut self) -> u8 {
        self.next_pat = self.next_pat.wrapping_add(1);
        if self.next_pat == 0 {
            self.next_pat = 1;
        }
        self.next_pat
    }

    /// Remove owners the slab reported evicted; a pinned owner being evicted is a
    /// hard failure.
    fn reconcile(&mut self) -> Result<(), String> {
        let evicted: Vec<usize> = EVICTED.with(|e| core::mem::take(&mut *e.borrow_mut()));
        for owner in evicted {
            match self.live.iter().position(|l| l.owner == owner) {
                Some(idx) => {
                    if self.live[idx].pinned {
                        return Err(format!("pinned owner {owner} was evicted"));
                    }
                    self.live.remove(idx);
                }
                None => return Err(format!("evicted unknown owner {owner}")),
            }
        }
        Ok(())
    }

    fn check(&self) -> Result<(), String> {
        // Collect [ptr, end) ranges of everything that should be live & disjoint.
        let mut ranges: Vec<(usize, usize, &'static str)> = Vec::new();
        for l in &self.live {
            let a = l.ptr as usize;
            if !a.is_multiple_of(16) {
                return Err(format!("slot owner {} misaligned", l.owner));
            }
            if a < self.lo || a + self.slot_size > self.hi {
                return Err(format!("slot owner {} out of arena", l.owner));
            }
            if unsafe { deluge_usable_size(self.heap, l.ptr) } < self.slot_size {
                return Err(format!("slot owner {} usable < slot_size", l.owner));
            }
            for i in 0..self.slot_size {
                if unsafe { *l.ptr.add(i) } != l.pattern {
                    return Err(format!("slot owner {} corrupted at +{i}", l.owner));
                }
            }
            ranges.push((a, a + self.slot_size, "slot"));
        }
        for g in &self.generals {
            let a = g.ptr as usize;
            for i in 0..g.size {
                if unsafe { *g.ptr.add(i) } != g.pattern {
                    return Err("general alloc corrupted".to_string());
                }
            }
            ranges.push((a, a + g.size, "general"));
        }
        ranges.sort_by_key(|r| r.0);
        for w in ranges.windows(2) {
            if w[0].1 > w[1].0 {
                return Err(format!("live aliasing: {} and {} overlap", w[0].2, w[1].2));
            }
        }
        Ok(())
    }

    pub fn step(&mut self, op: SlabOp) -> Result<(), String> {
        match op {
            SlabOp::Acquire => {
                let owner = self.next_owner;
                self.next_owner += 1;
                let p = unsafe { deluge_slab_acquire(self.slab, owner as *mut c_void) };
                if !p.is_null() {
                    let pattern = self.pat();
                    unsafe { core::ptr::write_bytes(p, pattern, self.slot_size) };
                    self.live.push(SlabLive {
                        ptr: p,
                        owner,
                        pinned: false,
                        pattern,
                    });
                }
            }
            SlabOp::Pin(i) => {
                if !self.live.is_empty() {
                    let j = i % self.live.len();
                    if !self.live[j].pinned {
                        unsafe { deluge_slab_pin(self.slab, self.live[j].ptr) };
                        self.live[j].pinned = true;
                    }
                }
            }
            SlabOp::Unpin(i) => {
                if !self.live.is_empty() {
                    let j = i % self.live.len();
                    if self.live[j].pinned {
                        unsafe { deluge_slab_unpin(self.slab, self.live[j].ptr) };
                        self.live[j].pinned = false;
                    }
                }
            }
            SlabOp::Touch(i) => {
                if !self.live.is_empty() {
                    let j = i % self.live.len();
                    unsafe { deluge_slab_touch(self.slab, self.live[j].ptr) };
                }
            }
            SlabOp::Release(i) => {
                if !self.live.is_empty() {
                    let j = i % self.live.len();
                    let l = self.live.remove(j);
                    unsafe { deluge_slab_release(self.slab, l.ptr) };
                }
            }
            SlabOp::GeneralAlloc(size) => {
                let size = (size % 65536) + 1;
                let p = unsafe { deluge_alloc(self.heap, size, 16) };
                if !p.is_null() {
                    let pattern = self.pat();
                    unsafe { core::ptr::write_bytes(p, pattern, size) };
                    self.generals.push(General {
                        ptr: p,
                        size,
                        pattern,
                    });
                }
            }
            SlabOp::GeneralFree(i) => {
                if !self.generals.is_empty() {
                    let j = i % self.generals.len();
                    let g = self.generals.remove(j);
                    unsafe { deluge_free(self.heap, g.ptr) };
                }
            }
        }
        self.reconcile()?;
        self.check()
    }
}

/// Decode fuzzer bytes into a slab op sequence and run it (Err == crash).
pub fn run_slab_bytes(data: &[u8]) -> Result<(), String> {
    let mut m = SlabModel::new(128 * 1024, 4096, 16);
    let mut it = data.iter().copied();
    while let Some(tag) = it.next() {
        let a = it.next().unwrap_or(0) as usize;
        let b = it.next().unwrap_or(0) as usize;
        let arg = a | (b << 8);
        let op = match tag % 7 {
            0 => SlabOp::Acquire,
            1 => SlabOp::Pin(arg),
            2 => SlabOp::Unpin(arg),
            3 => SlabOp::Touch(arg),
            4 => SlabOp::Release(arg),
            5 => SlabOp::GeneralAlloc(arg),
            _ => SlabOp::GeneralFree(arg),
        };
        m.step(op)?;
    }
    Ok(())
}
