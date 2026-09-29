//! Which execution tier is running the caller: interaction (thread mode: the UI, the sequencer, the app's
//! tasks), storage (the storage tasks) or audio (the render). The north-star's first invariant is stated in
//! these terms: a context may block only on work owned by a strictly higher-priority tier.
//!
//! On the device the tier is DERIVED from the two interrupt executors' own marks: the audio SGI handler marks
//! itself (`services::deluge_in_audio_context`), and the storage executor's SGI handler takes [`storage_isr`].
//! The CPU mode cannot do this: the shared IRQ entry switches to SYS mode, the mode thread code runs in, before
//! it dispatches, so `deluge_in_interrupt()` is false inside every handler. On host there is no interrupt state, so the tier is a thread-local TAG: the storage
//! tasks wrap their bodies in [`scoped`], and the audio render takes [`enter`]. Tagging the task rather than
//! the executor is what works on every host layout, including the golden runner's single executor.
use core::future::Future;
use core::sync::atomic::{AtomicU32, Ordering};

/// An execution tier. Numbered for the C ABI (`libdeluge/tier.h`).
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tier {
    Interaction = 0,
    Storage = 1,
    Audio = 2,
}

/// Nesting depth of the storage executor's SGI handler. A counter, not a flag, for the same reason as the audio
/// mark: a handler can nest, and on exit the mark must revert to the enclosing context.
static STORAGE_ISR_DEPTH: AtomicU32 = AtomicU32::new(0);

/// Marks the storage executor's SGI handler until dropped. See [`storage_isr`].
pub struct StorageIsr;

/// Mark entry into the storage executor's SGI handler; the mark lasts until the returned value drops. Call ONLY
/// from that handler, around the executor's poll.
pub fn storage_isr() -> StorageIsr {
    STORAGE_ISR_DEPTH.fetch_add(1, Ordering::Relaxed);
    StorageIsr
}

impl Drop for StorageIsr {
    fn drop(&mut self) {
        STORAGE_ISR_DEPTH.fetch_sub(1, Ordering::Relaxed);
    }
}

/// The device rule: audio if the audio handler is on the stack (it preempts everything), else storage if the
/// storage handler is, else interaction. Compiled on every target so the rule itself is testable on host.
pub fn derive(in_audio: bool) -> Tier {
    if in_audio {
        Tier::Audio
    } else if STORAGE_ISR_DEPTH.load(Ordering::Relaxed) != 0 {
        Tier::Storage
    } else {
        Tier::Interaction
    }
}

/// The tier running the caller.
#[cfg(target_os = "none")]
pub fn current() -> Tier {
    derive(crate::services::deluge_in_audio_context())
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
