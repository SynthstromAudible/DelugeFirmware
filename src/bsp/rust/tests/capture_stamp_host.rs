//! Tests capture stamps: placing input events on the TX play timeline and converting them to render
//! offsets.

use deluge_bsp_rust::audio::capture_stamp;

use capture_stamp::{INPUT_LATENCY_FRAMES, input_offset, stamp_at};

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
    // 44 frames before play frame 10 is frame 990 of the previous lap: 434 frames behind a write
    // head at 400.
    assert_eq!(stamp & MASK, 990);
    assert_eq!(input_offset(stamp, 400, MASK), INPUT_LATENCY_FRAMES - 434);
}

#[test]
fn spacing_between_events_survives_reading_them_at_different_times() {
    // Two edges 10 ms apart (441 frames); the first read 3 ms late, the second 1 ms late.
    let first = stamp_at(132 + 132, 13_000, 10_000, TICK_HZ, RATE, MASK);
    let second = stamp_at(132 + 441 + 44, 21_000, 20_000, TICK_HZ, RATE, MASK);
    let write_end = 700;
    let a = input_offset(first, write_end, MASK);
    let b = input_offset(second, write_end, MASK);
    assert_eq!(b - a, 441);
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

mod input_offset {
    //! The offset an input event takes effect at in the next render, under the fixed-block drive:
    //! the render writes 128-frame blocks and the app's sample timer advances 128 per block, so the
    //! sample the next render starts with plays when the codec reaches `write_end`.

    use deluge_bsp_rust::audio::block_drive::BLOCK_FRAMES;
    use deluge_bsp_rust::audio::capture_stamp::{INPUT_LATENCY_FRAMES, input_offset};

    const MASK: u32 = 1023;
    const BLOCK: u64 = BLOCK_FRAMES as u64;

    /// The play frame of the sample an event at unwrapped play frame `event` lands on, read with
    /// the write head at unwrapped `write_end`: the next rendered sample plays at `write_end`, and
    /// the event lands `input_offset` samples after it.
    fn lands_at(event: u64, write_end: u64) -> u64 {
        write_end
            + u64::from(input_offset(
                event as u32 & MASK,
                write_end as u32 & MASK,
                MASK,
            ))
    }

    /// Every event read promptly lands a constant INPUT_LATENCY_FRAMES after it, wherever in a
    /// block it falls: no event is a block early or late.
    #[test]
    fn an_event_read_promptly_lands_at_a_constant_latency_at_every_phase_of_a_block() {
        // Steady state: the block at [b, b+128) plays, three more are written ahead of it.
        for block in 10..20u64 {
            let play_block_start = block * BLOCK;
            let write_end = play_block_start + 4 * BLOCK;
            for phase in 0..BLOCK {
                let event = play_block_start + phase;
                assert_eq!(
                    lands_at(event, write_end),
                    event + u64::from(INPUT_LATENCY_FRAMES),
                    "block {block} phase {phase}"
                );
            }
        }
    }

    /// An event read after a further block was rendered still lands at the same latency.
    #[test]
    fn an_event_read_a_block_later_lands_at_the_same_latency() {
        let event = 5_000u64;
        let write_end = (event / BLOCK) * BLOCK + 4 * BLOCK;
        assert_eq!(
            lands_at(event, write_end),
            lands_at(event, write_end + BLOCK)
        );
    }

    /// Two edges 10 ms apart land 441 frames apart, though read at different write positions.
    #[test]
    fn spacing_survives_reads_at_different_write_positions() {
        let first = 7_000u64;
        let second = first + 441;
        let a = lands_at(first, (first / BLOCK) * BLOCK + 4 * BLOCK);
        let b = lands_at(second, (second / BLOCK) * BLOCK + 5 * BLOCK);
        assert_eq!(b - a, 441);
    }

    /// An event read so late that its place is already rendered takes effect at once, never a ring
    /// later.
    #[test]
    fn an_event_whose_place_is_already_rendered_takes_effect_at_once() {
        let event = 9_000u64;
        let write_end = event + u64::from(INPUT_LATENCY_FRAMES) + 300;
        assert_eq!(
            input_offset(event as u32 & MASK, write_end as u32 & MASK, MASK),
            0
        );
    }

    /// An event at the write head (the host's "now") takes effect a full latency later.
    #[test]
    fn an_event_at_the_write_head_takes_effect_a_latency_later() {
        assert_eq!(input_offset(200, 200, MASK), INPUT_LATENCY_FRAMES);
    }
}
