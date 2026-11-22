//! Critical-section helpers guarding the manager's shared `Cell` state.
//!
//! Every access to that state goes through [`m_get`], [`m_set`] or [`m_rmw`], which wrap one
//! O(1) access in a [`Masked`] window. The window is asymmetric: every context masks except the
//! audio render context, which preempts every other context that touches the manager and so is
//! already atomic with respect to them. See `docs/dev/known-concurrency-bugs.md` (B1).
//!
//! On the Rust BSP there are three such contexts: audio (highest priority, skips the mask), the
//! streaming fill task (its own interrupt executor, masks), and thread mode (masks).
//!
//! On host, where the TSan harness runs audio and streaming on their own OS threads and
//! `deluge_in_audio_context()` is always false, every thread masks, so the global
//! critical-section mutex serializes them.

use core::cell::Cell;

// Provided by the BSP (`services.rs`); in this crate's own tests, by `stubs` below.
unsafe extern "C" {
    pub fn ENTER_CRITICAL_SECTION();
    pub fn EXIT_CRITICAL_SECTION();
    pub fn deluge_in_audio_context() -> bool;
}

/// RAII asymmetric critical section: masks unless called from the audio render context.
///
/// The audio context preempts every other context that touches the manager, so its accesses are
/// already atomic and it skips the mask.
///
/// The predicate is deliberately "am I the audio context", not "am I in an interrupt": the
/// streaming fill task runs in an interrupt context that audio preempts, so it must mask. Gating on
/// `deluge_in_interrupt()` would silently drop its critical sections and race the audio path.
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

/// Masked get→modify→set over a single cell; the whole window is indivisible with respect to
/// every other manager context.
#[inline]
pub fn m_rmw<T: Copy, R>(c: &Cell<T>, f: impl FnOnce(&mut T) -> R) -> R {
    let _m = Masked::enter();
    let mut v = c.get();
    let r = f(&mut v);
    c.set(v);
    r
}

// Test-only providers of the BSP symbols, backed by the real `critical-section` std impl, with
// settable context flags and ENTER/EXIT counters so the asymmetry is assertable. The state is
// thread-local because `cargo test` runs tests in parallel and a global counter would let one
// test pollute another's counts. `pub(crate)` so other modules' tests share this harness.
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

    /// Monotonic stand-in for the audio sample timer; tests never assert on its value.
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

    /// Models the audio ISR, which is both "in an interrupt" and "the audio context". Setting
    /// only the in-interrupt flag models the streaming interrupt context instead, which masks.
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
    fn thread_mode_masks_rmw() {
        cs_reset_counts();
        deluge_in_interrupt_set(false); // thread mode
        let c = Cell::new(1u32);
        m_rmw(&c, |v| *v += 1);
        assert_eq!(c.get(), 2);
        assert_eq!(
            cs_enter_count(),
            1,
            "thread-mode RMW must mask exactly once"
        );
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
        // The streaming fill task runs in an interrupt context that audio preempts, so it must
        // mask; gating on "am I in any interrupt" would make this a no-op and race audio.
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
