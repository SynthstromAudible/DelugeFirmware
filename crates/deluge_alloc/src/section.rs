//! One context in the heap at a time.
//!
//! On the device the audio render preempts every other context that touches a heap, and the heap
//! has no lock of its own. So every heap step — one TLSF malloc/free/shrink, one reclaim-guard
//! flip, one slab slot's check-and-set — runs inside a `Section`: every context but the audio
//! render masks for the step; the render needs nothing, because nothing that touches a heap
//! preempts it.
//!
//! The app registers the hooks when it builds its heaps (`src/deluge/memory/heaps.cpp`), before any
//! other context runs: `enter` masks unless the caller is the render and says whether it did,
//! `exit` unmasks, and `note` reports each step to the app's structural-edit probe. Only O(1) work
//! goes inside a section — never the reclaim hook, a copy, or a table scan.

use core::sync::atomic::{AtomicUsize, Ordering};

/// Masks unless the caller is the audio render; returns whether it masked. Called from task, ISR
/// and audio contexts.
pub type SectionEnterFn = unsafe extern "C" fn() -> bool;
/// Unmasks what a [`SectionEnterFn`] that returned true masked. Called from task and ISR contexts.
pub type SectionExitFn = unsafe extern "C" fn();
/// Reports one heap step of `kind` ([`STEP_HEAP`] or [`STEP_SLAB`]). Called from task, ISR and
/// audio contexts.
pub type StepNoteFn = unsafe extern "C" fn(kind: u8);

/// A TLSF step: malloc, free, in-place shrink, usable size, or the reclaim guard.
pub const STEP_HEAP: u8 = 0;
/// A slab slot's claim, clear, pin, unpin or touch.
pub const STEP_SLAB: u8 = 1;

static ENTER: AtomicUsize = AtomicUsize::new(0);
static EXIT: AtomicUsize = AtomicUsize::new(0);
static NOTE: AtomicUsize = AtomicUsize::new(0);

/// Registers the section hooks; a null hook does nothing.
///
/// # Safety
///
/// Call once at boot, before any other context touches a heap. Each hook must be callable from
/// every context that uses a heap.
#[no_mangle]
pub unsafe extern "C" fn deluge_heap_set_section_hooks(
    enter: Option<SectionEnterFn>,
    exit: Option<SectionExitFn>,
    note: Option<StepNoteFn>,
) {
    ENTER.store(enter.map_or(0, |f| f as usize), Ordering::Relaxed);
    EXIT.store(exit.map_or(0, |f| f as usize), Ordering::Relaxed);
    NOTE.store(note.map_or(0, |f| f as usize), Ordering::Relaxed);
}

/// RAII guard for one heap step: enters, then reports the step from inside the section.
pub(crate) struct Section {
    entered: bool,
}

impl Section {
    #[inline]
    pub(crate) fn enter(kind: u8) -> Self {
        let enter = ENTER.load(Ordering::Relaxed);
        // SAFETY: ENTER only ever holds 0 or a SectionEnterFn.
        let entered =
            enter != 0 && unsafe { core::mem::transmute::<usize, SectionEnterFn>(enter)() };
        let note = NOTE.load(Ordering::Relaxed);
        if note != 0 {
            // SAFETY: NOTE only ever holds 0 or a StepNoteFn.
            unsafe { core::mem::transmute::<usize, StepNoteFn>(note)(kind) };
        }
        Section { entered }
    }
}

impl Drop for Section {
    #[inline]
    fn drop(&mut self) {
        if self.entered {
            let exit = EXIT.load(Ordering::Relaxed);
            if exit != 0 {
                // SAFETY: EXIT only ever holds 0 or a SectionExitFn.
                unsafe { core::mem::transmute::<usize, SectionExitFn>(exit)() };
            }
        }
    }
}

/// Test hooks shared by every test in this crate.
///
/// Hooks are process-wide, so one set is registered once and each thread picks its behaviour
/// through `MODE`: `Record` (the default) logs events and counts depth; the stress modes model the
/// host section with a spin lock the render holds for its whole block.
#[cfg(test)]
pub(crate) mod test_hooks {
    use super::*;
    use core::cell::{Cell, RefCell};
    use core::sync::atomic::AtomicBool;
    use std::sync::Once;

    #[derive(Clone, Copy, PartialEq, Eq)]
    pub enum Mode {
        Record,
        StressRender,
        StressInteraction,
        /// A neutered interaction context: never masks. Used only when checking the stress by hand.
        #[allow(dead_code)]
        StressUnguarded,
    }

    thread_local! {
        pub static MODE: Cell<Mode> = const { Cell::new(Mode::Record) };
        pub static EVENTS: RefCell<Vec<&'static str>> = const { RefCell::new(Vec::new()) };
        pub static DEPTH: Cell<u32> = const { Cell::new(0) };
    }

    pub static LOCK: AtomicBool = AtomicBool::new(false);

    pub fn lock() {
        while LOCK
            .compare_exchange_weak(false, true, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            core::hint::spin_loop();
        }
    }
    pub fn unlock() {
        LOCK.store(false, Ordering::Release);
    }

    unsafe extern "C" fn enter() -> bool {
        match MODE.with(Cell::get) {
            Mode::Record => {
                EVENTS.with(|e| e.borrow_mut().push("enter"));
                DEPTH.with(|d| d.set(d.get() + 1));
                true
            }
            Mode::StressInteraction => {
                lock();
                true
            }
            Mode::StressRender | Mode::StressUnguarded => false,
        }
    }
    unsafe extern "C" fn exit() {
        match MODE.with(Cell::get) {
            Mode::Record => {
                EVENTS.with(|e| e.borrow_mut().push("exit"));
                DEPTH.with(|d| d.set(d.get() - 1));
            }
            Mode::StressInteraction => unlock(),
            Mode::StressRender | Mode::StressUnguarded => {}
        }
    }
    unsafe extern "C" fn note(kind: u8) {
        let m = MODE.with(Cell::get);
        // A stress thread takes no heap step without the lock held; a step outside it would make
        // the stress prove nothing.
        if (m == Mode::StressRender || m == Mode::StressInteraction)
            && !LOCK.load(Ordering::Relaxed)
        {
            eprintln!("stress model broken: a heap step (kind {kind}) outside the lock");
            std::process::abort();
        }
        if m == Mode::Record {
            EVENTS.with(|e| {
                e.borrow_mut()
                    .push(if kind == STEP_HEAP { "heap" } else { "slab" })
            });
        }
    }

    static INSTALL: Once = Once::new();
    /// Registers the test hooks (idempotent) and clears this thread's event log.
    pub fn install() {
        INSTALL.call_once(|| unsafe {
            deluge_heap_set_section_hooks(Some(enter), Some(exit), Some(note))
        });
        EVENTS.with(|e| e.borrow_mut().clear());
    }
    pub fn take_events() -> Vec<&'static str> {
        EVENTS.with(|e| core::mem::take(&mut *e.borrow_mut()))
    }
}
