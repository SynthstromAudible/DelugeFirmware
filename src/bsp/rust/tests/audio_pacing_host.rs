//! Tests the audio drive's render-window pacing ([`pace_window`]) against the arithmetic of the
//! app's original `AudioEngine::routine_()`.
#![cfg(not(target_os = "none"))]

use deluge_bsp_rust::audio::pacing;

use pacing::pace_window;

/// The ring the device drive paces against (its `APP_BLOCK_FRAMES`).
const RING: usize = 128;

/// The reference window: the adjustment the app's `AudioEngine::routine_()` computed.
fn original_routine_window(mut n: usize) -> usize {
    let threshold: i32 = 6;
    if n < RING {
        let over = n as i32 - threshold;
        if over > 0 {
            n = (threshold + (over << 1)) as usize;
        }
    }
    n = n.min(RING);
    if n >= 3 {
        n = (n + 2) & !3;
    }
    n
}

#[test]
fn the_window_matches_the_original_arithmetic_for_every_raw_count() {
    for raw in 0..=4 * RING {
        assert_eq!(
            pace_window(raw, RING),
            original_routine_window(raw),
            "raw={raw}"
        );
    }
}

#[test]
fn the_window_never_exceeds_the_ring_and_is_a_multiple_of_four_from_three_up() {
    for raw in 0..=4 * RING {
        let w = pace_window(raw, RING);
        assert!(w <= RING, "raw={raw} window={w}");
        if raw >= 3 {
            assert_eq!(w % 4, 0, "raw={raw} window={w}");
        }
    }
}
