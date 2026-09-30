//! `cargo test` entry point for the audio drive's render-window pacing (`src/audio_pacing.rs`), which runs only on the
//! device.
//!
//!   cargo test --test audio_pacing_host
#![cfg(not(target_os = "none"))]

use deluge_bsp_rust::audio_pacing;

use audio_pacing::pace_window;

/// The ring the device's drive paces against (`audio.rs`'s `APP_BLOCK_FRAMES`).
const RING: usize = 128;

/// The window adjustment as AudioEngine::routine_() computed it before the drive moved into the BSP, kept as the
/// reference the relocated arithmetic must reproduce.
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
