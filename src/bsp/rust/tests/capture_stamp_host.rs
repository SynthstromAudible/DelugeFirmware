//! Tests capture stamps: placing input events on the TX play timeline and converting them to render
//! offsets.

use deluge_bsp_rust::audio::capture_stamp;

use capture_stamp::{render_offset, stamp_at};

/// 1 MHz, the device's Embassy tick rate.
const TICK_HZ: u64 = 1_000_000;
const RATE: u32 = 44_100;
/// The TX ring is 1024 frames.
const MASK: u32 = 1023;

#[test]
fn an_event_read_at_once_is_stamped_with_the_play_position() {
    assert_eq!(stamp_at(500, 7_000, 7_000, TICK_HZ, RATE, MASK), 500);
}

#[test]
fn an_event_one_millisecond_old_is_stamped_44_frames_back() {
    assert_eq!(stamp_at(500, 8_000, 7_000, TICK_HZ, RATE, MASK), 456);
}

#[test]
fn a_stamp_before_the_ring_start_wraps_and_still_converts() {
    let stamp = stamp_at(10, 8_000, 7_000, TICK_HZ, RATE, MASK);
    // 44 frames before play frame 10 is frame 990 of the previous lap.
    assert_eq!(render_offset(stamp, 990, MASK), 0);
    assert_eq!(render_offset(stamp, 980, MASK), 10);
}

#[test]
fn spacing_between_events_survives_reading_them_at_different_times() {
    // Two edges 10 ms apart (441 frames); the first read 3 ms late, the second 1 ms late.
    let first = stamp_at(132 + 132, 13_000, 10_000, TICK_HZ, RATE, MASK);
    let second = stamp_at(132 + 441 + 44, 21_000, 20_000, TICK_HZ, RATE, MASK);
    let block_start = 100;
    let a = render_offset(first, block_start, MASK);
    let b = render_offset(second, block_start, MASK);
    assert_eq!(b.wrapping_sub(a) & MASK, 441);
}

#[test]
fn an_event_stamped_in_the_future_is_treated_as_now() {
    assert_eq!(stamp_at(500, 7_000, 9_000, TICK_HZ, RATE, MASK), 500);
}

#[test]
fn a_late_event_clamps_to_the_ring() {
    // One second late is 44,100 frames: more than the ring holds. It clamps to MASK frames back.
    assert_eq!(
        stamp_at(500, 1_007_000, 7_000, TICK_HZ, RATE, MASK),
        500u32.wrapping_sub(MASK)
    );
}
