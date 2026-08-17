//! B1 synchronization primitives for the Manager. Every access to the Manager's
//! shared `Cell` state routes through [`m_get`] / [`m_set`] / [`m_rmw`], which
//! wrap the access in an asymmetric critical section (`Masked`): every context masks the
//! minimal O(1) window EXCEPT the audio render context, which skips the mask entirely
//! (gated on `!deluge_in_audio_context()`) because it preempts every other
//! manager-touching context, so its RMWs are already atomic w.r.t. them. See
//! docs/dev/known-concurrency-bugs.md (B1) and
//! docs/superpowers/specs/2026-07-19-manager-b1-sync-design.md.
//!
//! There are three such contexts on the Rust BSP: audio (highest, skips), the streaming
//! fill task (its own interrupt executor, masks), and thread mode / the worker fiber
//! (masks). The predicate is audio-specific rather than "in an interrupt" precisely so the
//! middle one keeps its mask — see [`Masked`].
//!
//! On host — where the preemptive TSan harness runs audio and streaming on real second and
//! third OS threads and `deluge_in_audio_context()` is always false — ALL threads mask, so
//! the global critical-section mutex serializes them and TSan sees no race.

use core::cell::Cell;

// The C-ABI critical-section primitives, provided by the BSP (services.rs) in
// the real device/host_app link. In the crate's own `cargo test` binary they
// are provided by the `#[cfg(test)]` stubs at the bottom of this file.
unsafe extern "C" {
    fn ENTER_CRITICAL_SECTION();
    fn EXIT_CRITICAL_SECTION();
    fn deluge_in_audio_context() -> bool;
}

/// RAII asymmetric critical section. Enters iff called outside the audio render context
/// (`!deluge_in_audio_context()`) — a no-op on the audio path, which is already atomic
/// w.r.t. every context that can touch the manager because it preempts all of them, and
/// near-no-op on the cooperative BSP (nothing to preempt).
///
/// The predicate is deliberately "am I the audio context", NOT "am I in an interrupt":
/// the streaming fill task runs in its own interrupt context that audio preempts, so it
/// must mask like any other preemptible caller. Gating on `deluge_in_interrupt()` would
/// make its critical sections silently vanish and race the audio path.
pub struct Masked {
    active: bool,
}

impl Masked {
    #[inline]
    pub fn enter() -> Self {
        // SAFETY: FFI to the BSP interrupt-mask primitives; no invariants beyond
        // ENTER/EXIT being balanced, which the Drop impl guarantees.
        let active = unsafe { !deluge_in_audio_context() };
        if active {
            unsafe { ENTER_CRITICAL_SECTION() };
        }
        Masked { active }
    }
}

impl Drop for Masked {
    #[inline]
    fn drop(&mut self) {
        if self.active {
            // SAFETY: balanced with the ENTER in `enter`.
            unsafe { EXIT_CRITICAL_SECTION() };
        }
    }
}

/// Masked coherent read of a single cell. The mask is released on return, so a
/// scan that calls this per slot holds the mask for only one slot at a time.
#[inline]
pub fn m_get<T: Copy>(c: &Cell<T>) -> T {
    let _m = Masked::enter();
    c.get()
}

/// Masked write of a single cell.
#[inline]
pub fn m_set<T>(c: &Cell<T>, v: T) {
    let _m = Masked::enter();
    c.set(v);
}

/// Masked get→modify→set window over a single cell. The whole window is
/// indivisible w.r.t. the fiber (and atomic on the audio path).
#[inline]
pub fn m_rmw<T: Copy, R>(c: &Cell<T>, f: impl FnOnce(&mut T) -> R) -> R {
    let _m = Masked::enter();
    let mut v = c.get();
    let r = f(&mut v);
    c.set(v);
    r
}

// ---- test-only providers of the C-ABI primitives -----------------------------
// In `cargo test -p deluge_resource` there is no BSP in the link, so provide the
// three extern symbols here, backed by the real `critical-section` std impl (so
// the guard is exercised for real), plus a settable in-interrupt flag and
// ENTER/EXIT counters so the asymmetry is directly assertable.
//
// The ENTER/EXIT counters are thread-local (not global atomics): `cargo test`
// runs test functions on multiple threads in parallel, and a global counter
// would let one test's m_rmw calls pollute another concurrently-running test's
// enter/exit-count assertions. Thread-local counters make each test's counts
// depend only on that test's own thread, matching IN_ISR/DEPTH/TOKEN below.
// `pub(crate)` (not private) so sibling modules' tests — notably `facade`'s
// `Lease::drop` ISR-safety proof — can drive the same two-context model
// (`deluge_in_interrupt_set`) and read the same critical-section counters this
// module's own tests use, rather than re-deriving a second harness.
#[cfg(test)]
pub(crate) mod stubs {
    use core::cell::Cell;

    std::thread_local! {
        static IN_ISR: Cell<bool> = const { Cell::new(false) };
        static IN_AUDIO: Cell<bool> = const { Cell::new(false) };
        static DEPTH: Cell<u32> = const { Cell::new(0) };
        static TOKEN: Cell<Option<critical_section::RestoreState>> = const { Cell::new(None) };
        static ENTER_COUNT: Cell<u64> = const { Cell::new(0) };
        static EXIT_COUNT: Cell<u64> = const { Cell::new(0) };
        static FAKE_NOW: Cell<u32> = const { Cell::new(1) };
    }

    /// Host stand-in for the C++ audio sample timer the manager uses to measure loader service
    /// latency. A monotonic counter is enough — the tests never assert on the value, they just need
    /// the symbol to link.
    #[unsafe(no_mangle)]
    extern "C" fn deluge_debug_now_frames() -> u32 {
        FAKE_NOW.with(|c| {
            let n = c.get().wrapping_add(1);
            c.set(n);
            n
        })
    }

    #[unsafe(no_mangle)]
    extern "C" fn ENTER_CRITICAL_SECTION() {
        ENTER_COUNT.with(|c| c.set(c.get() + 1));
        DEPTH.with(|d| {
            if d.get() == 0 {
                // SAFETY: released by the matching EXIT once this thread's depth hits 0.
                let t = unsafe { critical_section::acquire() };
                TOKEN.with(|tok| tok.set(Some(t)));
            }
            d.set(d.get() + 1);
        });
    }

    #[unsafe(no_mangle)]
    extern "C" fn EXIT_CRITICAL_SECTION() {
        EXIT_COUNT.with(|c| c.set(c.get() + 1));
        let closed = DEPTH.with(|d| {
            let n = d.get().saturating_sub(1);
            d.set(n);
            n == 0
        });
        if closed {
            TOKEN.with(|tok| {
                if let Some(t) = tok.take() {
                    // SAFETY: stashed by this thread's outermost ENTER.
                    unsafe { critical_section::release(t) };
                }
            });
        }
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_interrupt() -> bool {
        IN_ISR.with(|f| f.get())
    }

    #[unsafe(no_mangle)]
    extern "C" fn deluge_in_audio_context() -> bool {
        IN_AUDIO.with(|f| f.get())
    }

    pub fn deluge_in_interrupt_set(v: bool) {
        IN_ISR.with(|f| f.set(v));
    }

    pub fn deluge_in_audio_context_set(v: bool) {
        IN_AUDIO.with(|f| f.set(v));
    }

    /// Model the audio ISR: on device that context is both "in an interrupt" and "the audio
    /// context", so a test that means "pretend we are the audio render" should set both
    /// rather than picking one. Setting only the in-interrupt flag models the *streaming*
    /// interrupt context instead — which masks, and is what
    /// `a_non_audio_interrupt_context_still_masks` covers.
    pub fn audio_isr_context_set(v: bool) {
        deluge_in_interrupt_set(v);
        deluge_in_audio_context_set(v);
    }
    pub fn cs_enter_count() -> u64 {
        ENTER_COUNT.with(|c| c.get())
    }
    pub fn cs_exit_count() -> u64 {
        EXIT_COUNT.with(|c| c.get())
    }
    pub fn cs_reset_counts() {
        ENTER_COUNT.with(|c| c.set(0));
        EXIT_COUNT.with(|c| c.set(0));
    }
}

#[cfg(test)]
mod tests {
    use super::stubs::*;
    use super::*;

    #[test]
    fn fiber_masks_rmw() {
        cs_reset_counts();
        deluge_in_interrupt_set(false); // fiber context
        let c = Cell::new(1u32);
        m_rmw(&c, |v| *v += 1);
        assert_eq!(c.get(), 2);
        assert_eq!(cs_enter_count(), 1, "fiber RMW must mask exactly once");
        assert_eq!(cs_exit_count(), 1);
    }

    #[test]
    fn audio_skips_mask() {
        cs_reset_counts();
        audio_isr_context_set(true); // audio ISR context
        let c = Cell::new(1u32);
        m_rmw(&c, |v| *v += 1);
        assert_eq!(c.get(), 2);
        assert_eq!(cs_enter_count(), 0, "audio path must not mask");
        assert_eq!(cs_exit_count(), 0);
        audio_isr_context_set(false); // restore for other tests on this thread
    }

    #[test]
    fn a_non_audio_interrupt_context_still_masks() {
        // The streaming fill task runs in an interrupt context that the audio context
        // preempts, so it may NOT skip the mask — unlike audio itself. Regression guard for
        // the third-context hazard: gating on "am I in any interrupt" would silently make
        // this a no-op and race the audio path.
        cs_reset_counts();
        deluge_in_interrupt_set(true); // in an ISR ...
        deluge_in_audio_context_set(false); // ... but not the audio one
        let c = Cell::new(1u32);
        m_rmw(&c, |v| *v += 1);
        assert_eq!(c.get(), 2);
        assert_eq!(
            cs_enter_count(),
            1,
            "a non-audio interrupt context must mask: audio preempts it"
        );
        assert_eq!(cs_exit_count(), 1);
        deluge_in_interrupt_set(false); // restore for other tests on this thread
    }

    #[test]
    fn nesting_masks_once_per_level() {
        cs_reset_counts();
        deluge_in_interrupt_set(false);
        let outer = Cell::new(0u32);
        let inner = Cell::new(0u32);
        m_rmw(&outer, |o| {
            *o += 1;
            m_rmw(&inner, |i| *i += 1); // nested masked access
        });
        assert_eq!(outer.get(), 1);
        assert_eq!(inner.get(), 1);
        assert_eq!(cs_enter_count(), 2, "one enter per masked call, nested");
        assert_eq!(cs_exit_count(), 2);
    }
}
