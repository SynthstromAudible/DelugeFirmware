//! The execution tier running the caller (`libdeluge/tier.h`): interaction (thread mode: UI,
//! sequencer, app tasks), storage (the storage tasks) or audio (the render).
//!
//! The governing invariant: a context may block only on work owned by a strictly higher-priority
//! tier.
//!
//! On device the tier is derived from the interrupt executors' own marks: the audio SGI handler's
//! (`services::deluge_in_audio_context`) and the storage executor's ([`storage_isr`]). The CPU mode
//! cannot tell: the shared IRQ entry switches to SYS mode before dispatch, so
//! `deluge_in_interrupt()` is false inside every handler.
//!
//! On host the tier is a thread-local tag: storage tasks wrap their bodies in [`scoped`] and the
//! audio render takes [`enter`]. Tagging the task rather than the executor works on every host
//! layout, including the golden runner's single executor.
use core::future::Future;
use core::sync::atomic::{AtomicU32, Ordering};

/// An execution tier, numbered as `libdeluge/tier.h`'s `DelugeTier`.
#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Tier {
    Interaction = 0,
    Storage = 1,
    Audio = 2,
}

/// Nesting depth of the storage executor's SGI handler; a counter because the handler can nest.
static STORAGE_ISR_DEPTH: AtomicU32 = AtomicU32::new(0);

/// Marks the storage executor's SGI handler as running until dropped.
pub struct StorageIsr;

/// Marks entry into the storage executor's SGI handler until the returned value drops.
///
/// Call only from that handler, around the executor's poll.
pub fn storage_isr() -> StorageIsr {
    STORAGE_ISR_DEPTH.fetch_add(1, Ordering::Relaxed);
    StorageIsr
}

impl Drop for StorageIsr {
    fn drop(&mut self) {
        STORAGE_ISR_DEPTH.fetch_sub(1, Ordering::Relaxed);
    }
}

/// Returns the tier by the device rule: audio if the audio handler is on the stack, else storage if
/// the storage handler is, else interaction.
///
/// Compiled on every target so the rule is testable on host.
pub fn derive(in_audio: bool) -> Tier {
    if in_audio {
        Tier::Audio
    } else if STORAGE_ISR_DEPTH.load(Ordering::Relaxed) != 0 {
        Tier::Storage
    } else {
        Tier::Interaction
    }
}

/// Returns the tier running the caller.
#[cfg(target_os = "none")]
pub fn current() -> Tier {
    derive(crate::services::deluge_in_audio_context())
}

#[cfg(not(target_os = "none"))]
std::thread_local! {
    static CURRENT: core::cell::Cell<Tier> = const { core::cell::Cell::new(Tier::Interaction) };
}

/// Returns the tier running the caller.
#[cfg(not(target_os = "none"))]
pub fn current() -> Tier {
    CURRENT.with(|c| c.get())
}

/// Restores, on drop, the tier [`enter`] replaced.
pub struct Guard {
    #[cfg(not(target_os = "none"))]
    prev: Tier,
}

/// Tags the caller as running on `t` until the returned guard drops.
///
/// A no-op on device, where the tier is derived.
#[cfg(not(target_os = "none"))]
pub fn enter(t: Tier) -> Guard {
    Guard {
        prev: CURRENT.with(|c| c.replace(t)),
    }
}

/// Tags the caller as running on `t` until the returned guard drops.
///
/// A no-op on device, where the tier is derived.
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

/// Runs `f` tagged as tier `t`.
///
/// The tag is set only while `f` is polled, so it never leaks into other futures on the executor.
pub async fn scoped<F: Future>(t: Tier, f: F) -> F::Output {
    let mut f = core::pin::pin!(f);
    core::future::poll_fn(move |cx| {
        let _tier = enter(t);
        f.as_mut().poll(cx)
    })
    .await
}

/// `libdeluge/tier.h`'s `DelugeTier`.
///
/// Spelled here because the plain host build's `sys` lacks it; the pins below hold it to the
/// header.
pub type DelugeTier = u8;

/// Returns the tier running the caller; the C-ABI form of [`current`].
#[unsafe(no_mangle)]
pub extern "C" fn deluge_tier_current() -> DelugeTier {
    current() as DelugeTier
}

abi_pin!(deluge_tier_current);
abi_layout!(type DelugeTier => sys DelugeTier);
abi_const!(
    Tier::Interaction as u8 => DelugeTier_DELUGE_TIER_INTERACTION,
    Tier::Storage as u8 => DelugeTier_DELUGE_TIER_STORAGE,
    Tier::Audio as u8 => DelugeTier_DELUGE_TIER_AUDIO,
);
