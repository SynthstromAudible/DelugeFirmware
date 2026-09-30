//! Host tests for the tier tag: the default, nesting, and the future wrapper.
#![cfg(not(target_os = "none"))]

#[macro_use]
#[path = "support/abi_pin_noop.rs"]
mod abi_pin_noop;

#[path = "../src/tier.rs"]
mod tier;

use core::future::Future;
use core::pin::pin;
use core::task::{Context, Poll, Waker};
use tier::Tier;

#[test]
fn default_is_interaction() {
    assert_eq!(tier::current(), Tier::Interaction);
}

#[test]
fn nested_enter_restores() {
    let outer = tier::enter(Tier::Storage);
    assert_eq!(tier::current(), Tier::Storage);
    {
        let _inner = tier::enter(Tier::Audio);
        assert_eq!(tier::current(), Tier::Audio);
    }
    assert_eq!(tier::current(), Tier::Storage);
    drop(outer);
    assert_eq!(tier::current(), Tier::Interaction);
}

/// Pending once, recording the tier it saw on each poll.
struct Probe {
    polls: u32,
    seen: [Option<Tier>; 2],
}
impl Future for Probe {
    type Output = [Option<Tier>; 2];
    fn poll(mut self: core::pin::Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let i = self.polls as usize;
        self.seen[i] = Some(tier::current());
        self.polls += 1;
        if self.polls == 1 {
            cx.waker().wake_by_ref();
            Poll::Pending
        } else {
            Poll::Ready(self.seen)
        }
    }
}

#[test]
fn scoped_tags_only_while_polling() {
    let mut cx = Context::from_waker(Waker::noop());
    let mut fut = pin!(tier::scoped(
        Tier::Storage,
        Probe {
            polls: 0,
            seen: [None, None]
        }
    ));
    assert!(fut.as_mut().poll(&mut cx).is_pending());
    // Between polls the caller's tier is back.
    assert_eq!(tier::current(), Tier::Interaction);
    match fut.as_mut().poll(&mut cx) {
        Poll::Ready(seen) => assert_eq!(seen, [Some(Tier::Storage), Some(Tier::Storage)]),
        Poll::Pending => panic!("the probe completes on its second poll"),
    }
    assert_eq!(tier::current(), Tier::Interaction);
}

/// The device rule, evaluated on host. On the device, every IRQ handler runs in SYS mode (the shared
/// `_irq_handler` switches before dispatching), so the CPU mode cannot tell the storage executor's handler from
/// thread mode: the handler has to mark itself, as the audio one does.
#[test]
fn device_rule_derives_storage_only_inside_the_marked_handler() {
    assert_eq!(tier::derive(false), Tier::Interaction);
    {
        let _handler = tier::storage_isr();
        assert_eq!(tier::derive(false), Tier::Storage);
        // Audio preempts the storage handler.
        assert_eq!(tier::derive(true), Tier::Audio);
    }
    assert_eq!(tier::derive(false), Tier::Interaction);
}
