//! `sd::card_service` holds SD_BUS across the ~1.3 s identification of a newly inserted card, on the thread
//! executor. A thread-mode (interaction-tier) synchronous card call that arrives then must fail fast: it
//! spins, and the only executor that could resume `card_service` is the one spinning. A storage-tier caller,
//! on its own executor, must keep waiting and then succeed on the re-identified card.
#![cfg(not(target_os = "none"))]

use deluge_bsp_rust::sd;
use deluge_bsp_rust::sim_block;
use deluge_bsp_rust::tier;

use core::future::Future;
use core::pin::pin;
use core::task::{Context, Poll, Waker};

use deluge_bsp::sd as card;

/// The card's state and SD_BUS are process-global; the two tests take this for their whole body.
static TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

fn fresh_card() {
    let img = std::env::temp_dir().join(format!(
        "deluge-sd-bus-card-init-{}.img",
        std::process::id()
    ));
    // SAFETY: set under TEST_LOCK, before this test starts any thread.
    unsafe { std::env::set_var("DELUGE_SD_IMAGE", &img) };
    sim_block::block_on(card::init()).expect("the host card initializes");
}

#[test]
fn interaction_tier_read_during_identification_fails_fast() {
    let _serial = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    fresh_card();
    sim_block::set_spin_budget(10_000);

    // `card_service` mid-identification: it holds the bus, and the removal before it cleared readiness.
    let identifying = sd::SD_BUS.try_lock().expect("nothing else holds the bus");
    card::invalidate();

    let mut buf = [0u8; 512];
    let r = sim_block::block_on(sd::locked_read_sectors(0, 1, &mut buf));
    assert!(
        matches!(r, Err(card::SdError::NotInitialized)),
        "an interaction-tier read during identification must fail fast, got {r:?}"
    );

    drop(identifying);
    sim_block::block_on(card::init()).expect("re-identify");
    let r = sim_block::block_on(sd::locked_read_sectors(0, 1, &mut buf));
    assert!(
        r.is_ok(),
        "a read after identification must succeed, got {r:?}"
    );
}

#[test]
fn storage_tier_read_keeps_waiting_through_identification() {
    let _serial = TEST_LOCK.lock().unwrap_or_else(|e| e.into_inner());
    fresh_card();

    let identifying = sd::SD_BUS.try_lock().expect("nothing else holds the bus");
    card::invalidate();

    let _storage = tier::enter(tier::Tier::Storage);
    let mut buf = [0u8; 512];
    let mut cx = Context::from_waker(Waker::noop());
    let mut read = pin!(sd::locked_read_sectors(0, 1, &mut buf));
    assert!(
        read.as_mut().poll(&mut cx).is_pending(),
        "a storage-tier read must wait for identification, not fail"
    );

    drop(identifying);
    sim_block::block_on(card::init()).expect("re-identify");
    match read.as_mut().poll(&mut cx) {
        Poll::Ready(r) => assert!(
            r.is_ok(),
            "the waiting read must succeed on the re-identified card, got {r:?}"
        ),
        Poll::Pending => panic!("the bus is free and the card ready; the read must complete"),
    }
}
