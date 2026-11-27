//! Tests capture stamps: placing input events on the play timeline (the codec's frames counted from
//! the drive's first block) and converting them to render offsets.

use deluge_platform_device::audio::block_drive::BLOCK_FRAMES;
use deluge_platform_device::audio::capture_stamp;

use capture_stamp::{
    INPUT_LATENCY_FRAMES, STAMP_MAX_AGE_FRAMES, input_offset, stamp_at, timeline_frame,
};

/// 1 MHz, the device's Embassy tick rate.
const TICK_HZ: u64 = 1_000_000;
const RATE: u32 = 44_100;
/// The TX ring is 1024 frames.
const MASK: u32 = 1023;
const BLOCK: u32 = BLOCK_FRAMES as u32;

#[test]
fn an_event_read_at_once_is_stamped_with_the_play_position() {
    assert_eq!(stamp_at(500, 7_000, 7_000, TICK_HZ, RATE), 500);
}

#[test]
fn an_event_one_millisecond_old_is_stamped_44_frames_back() {
    assert_eq!(stamp_at(500, 8_000, 7_000, TICK_HZ, RATE), 456);
}

#[test]
fn an_event_stamped_in_the_future_is_treated_as_now() {
    assert_eq!(stamp_at(500, 7_000, 9_000, TICK_HZ, RATE), 500);
}

#[test]
fn a_very_old_event_clamps_to_the_maximum_age() {
    // Ten minutes late: clamped, not wrapped round the timeline.
    assert_eq!(
        stamp_at(1 << 28, 600_007_000, 7_000, TICK_HZ, RATE),
        (1 << 28) - STAMP_MAX_AGE_FRAMES
    );
}

mod timeline {
    use super::*;

    #[test]
    fn the_ring_position_counts_on_from_the_block_playing() {
        // Block 81 (ring block 1) plays: its first frame is timeline frame 81 * 128.
        assert_eq!(timeline_frame(128 + 5, 81, MASK), 81 * BLOCK + 5);
    }

    #[test]
    fn a_position_past_the_ring_end_still_counts_on() {
        // Block 7 of lap 10 plays (timeline block 87); the codec has wrapped to ring frame 3.
        assert_eq!(timeline_frame(3, 87, MASK), 88 * BLOCK + 3);
    }

    #[test]
    fn the_timeline_wraps_at_u32_like_the_stamps() {
        let block = u32::MAX / BLOCK; // near the end of the u32 timeline
        let f = timeline_frame((block % 8) * BLOCK + 7, block, MASK);
        assert_eq!(f, block.wrapping_mul(BLOCK).wrapping_add(7));
    }
}

mod input_offset {
    //! The offset an input event takes effect at in the next render, under the fixed-block drive:
    //! the render writes 128-frame blocks and the app's sample timer advances 128 per block, so the
    //! sample the next render starts with plays when the codec reaches `write_end`.

    use super::*;

    /// The timeline frame of the sample an event at timeline frame `event` lands on, read with
    /// the write head at `write_end`: the next rendered sample plays at `write_end`, and the event
    /// lands `input_offset` samples after it.
    fn lands_at(event: u32, write_end: u32) -> u32 {
        write_end + input_offset(event, write_end)
    }

    /// Every event read promptly lands a constant INPUT_LATENCY_FRAMES after it, wherever in a
    /// block it falls: no event is a block early or late.
    #[test]
    fn an_event_read_promptly_lands_at_a_constant_latency_at_every_phase_of_a_block() {
        // Steady state: the block at [b, b+128) plays, three more are written ahead of it.
        for block in 10..20u32 {
            let play_block_start = block * BLOCK;
            let write_end = play_block_start + 4 * BLOCK;
            for phase in 0..BLOCK {
                let event = play_block_start + phase;
                assert_eq!(
                    lands_at(event, write_end),
                    event + INPUT_LATENCY_FRAMES,
                    "block {block} phase {phase}"
                );
            }
        }
    }

    /// An event read after a further block was rendered still lands at the same latency.
    #[test]
    fn an_event_read_a_block_later_lands_at_the_same_latency() {
        let event = 5_000;
        let write_end = (event / BLOCK) * BLOCK + 4 * BLOCK;
        assert_eq!(
            lands_at(event, write_end),
            lands_at(event, write_end + BLOCK)
        );
    }

    /// Two edges 10 ms apart land 441 frames apart, though read at different write positions.
    #[test]
    fn spacing_survives_reads_at_different_write_positions() {
        let first = 7_000;
        let second = first + 441;
        let a = lands_at(first, (first / BLOCK) * BLOCK + 4 * BLOCK);
        let b = lands_at(second, (second / BLOCK) * BLOCK + 5 * BLOCK);
        assert_eq!(b - a, 441);
    }

    /// An event read so late that its place is already rendered takes effect at once.
    #[test]
    fn an_event_whose_place_is_already_rendered_takes_effect_at_once() {
        let event = 9_000;
        assert_eq!(input_offset(event, event + INPUT_LATENCY_FRAMES + 300), 0);
    }

    /// An event more than a ring behind the write head still takes effect at once: the timeline
    /// does not wrap at the ring, so it cannot alias to a recent event.
    #[test]
    fn an_event_more_than_a_ring_behind_takes_effect_at_once() {
        for behind in [1023, 1024, 1025, 1424, 2048 + 100, STAMP_MAX_AGE_FRAMES] {
            assert_eq!(
                input_offset(100_000u32.wrapping_sub(behind), 100_000),
                0,
                "behind {behind}"
            );
        }
    }

    /// A trigger edge read late lands at once, not after the fresh edge that follows it.
    #[test]
    fn a_stale_edge_never_lands_after_a_fresh_one() {
        let write_end = 50_000;
        let stale = input_offset(write_end - 1500, write_end);
        let fresh = input_offset(write_end - 450, write_end);
        assert!(stale <= fresh, "stale {stale} fresh {fresh}");
    }

    /// An event at the write head (or stamped before the first render set it) takes effect a
    /// full latency later.
    #[test]
    fn an_event_at_or_ahead_of_the_write_head_takes_effect_a_latency_later() {
        assert_eq!(input_offset(200, 200), INPUT_LATENCY_FRAMES);
        assert_eq!(input_offset(700, 0), INPUT_LATENCY_FRAMES);
    }

    /// A stamp converted from a real event clock: two edges 10 ms apart, read 3 ms and 1 ms late
    /// at different play positions, keep their 441-frame spacing.
    #[test]
    fn spacing_survives_stamping_at_different_times() {
        let first = stamp_at(10_000 + 132, 13_000, 10_000, TICK_HZ, RATE);
        let second = stamp_at(10_000 + 441 + 44, 21_000, 20_000, TICK_HZ, RATE);
        let write_end = 10_600;
        assert_eq!(
            input_offset(second, write_end) - input_offset(first, write_end),
            441
        );
    }
}

#[test]
fn a_render_offset_maps_to_the_frame_it_plays_at_and_never_to_now() {
    // The next rendered sample plays at write_end; offset k plays k frames later.
    assert_eq!(capture_stamp::stamp_for_offset(5_000, 0), 5_000);
    assert_eq!(capture_stamp::stamp_for_offset(5_000, 37), 5_037);
    // Frame 0 is a real frame; it must not read as "as soon as possible".
    assert_eq!(capture_stamp::stamp_for_offset(0, 0), 1);
    assert_eq!(capture_stamp::stamp_for_offset(u32::MAX, 1), 1);
}
