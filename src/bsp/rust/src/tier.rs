//! Which execution tier is running the caller: interaction (thread mode: the UI, the sequencer, the app's
//! tasks), storage (the storage tasks) or audio (the render). The north-star's first invariant is stated in
//! these terms: a context may block only on work owned by a strictly higher-priority tier.
//!
//! On the device the tier is DERIVED from interrupt state, with no bookkeeping: the audio SGI handler marks
//! itself (`services::deluge_in_audio_context`), and the only other interrupt-executed code is the storage
//! executor's SGI handler. On host there is no interrupt state, so the tier is a thread-local TAG: the storage
//! tasks wrap their bodies in [`scoped`], and the audio render takes [`enter`]. Tagging the task rather than
//! the executor is what works on every host layout, including the golden runner's single executor.
use core::future::Future;

/// An execution tier. Numbered for the C ABI (`libdeluge/tier.h`).
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tier {
    Interaction = 0,
    Storage = 1,
    Audio = 2,
}

/// The tier running the caller.
#[cfg(target_os = "none")]
pub fn current() -> Tier {
    if crate::services::deluge_in_audio_context() {
        Tier::Audio
    } else if crate::services::deluge_in_interrupt() {
        Tier::Storage
    } else {
        Tier::Interaction
    }
}

#[cfg(not(target_os = "none"))]
std::thread_local! {
    static CURRENT: core::cell::Cell<Tier> = const { core::cell::Cell::new(Tier::Interaction) };
}

/// The tier running the caller.
#[cfg(not(target_os = "none"))]
pub fn current() -> Tier {
    CURRENT.with(|c| c.get())
}

/// Restores the tier [`enter`] replaced when dropped.
pub struct Guard {
    #[cfg(not(target_os = "none"))]
    prev: Tier,
}

/// Tag the caller as running on `t` until the returned guard drops. A no-op on the device, where the tier is
/// derived.
#[cfg(not(target_os = "none"))]
pub fn enter(t: Tier) -> Guard {
    Guard {
        prev: CURRENT.with(|c| c.replace(t)),
    }
}

/// Tag the caller as running on `t` until the returned guard drops. A no-op on the device, where the tier is
/// derived.
#[cfg(target_os = "none")]
pub fn enter(_t: Tier) -> Guard {
    Guard {}
}

#[cfg(not(target_os = "none"))]
impl Drop for Guard {
    fn drop(&mut self) {
        CURRENT.with(|c| c.set(self.prev));
    }
}

/// Run `f` tagged as tier `t`: the tag is set only while `f` is being polled, so it never leaks into whatever
/// the executor polls between `f`'s polls.
pub async fn scoped<F: Future>(t: Tier, f: F) -> F::Output {
    let mut f = core::pin::pin!(f);
    core::future::poll_fn(move |cx| {
        let _tier = enter(t);
        f.as_mut().poll(cx)
    })
    .await
}

/// C-ABI twin of [`current`] (`include/libdeluge/tier.h`).
#[unsafe(no_mangle)]
pub extern "C" fn deluge_tier_current() -> u8 {
    current() as u8
}
