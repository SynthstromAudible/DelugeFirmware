//! Tests the fixed-block drive's scheduler: how many blocks each wake renders, where, and overruns,
//! including a stall long enough to lap the ring.

use deluge_bsp_rust::audio::block_drive::{BLOCK_FRAMES, BlockDrive, QUEUE_BLOCKS, RING_BLOCKS};

const BLOCK_US: u64 = 2902;

fn frame_of(block: usize) -> usize {
    (block % RING_BLOCKS) * BLOCK_FRAMES
}

/// A drive after its first wake, with the queue filled.
fn primed() -> BlockDrive {
    let mut d = BlockDrive::new();
    let p = d.plan(frame_of(0), 0);
    for _ in 0..p.renders {
        d.rendered();
    }
    d
}

#[test]
fn the_first_wake_fills_the_queue() {
    let mut d = BlockDrive::new();
    let p = d.plan(frame_of(0), 0);
    assert_eq!(p.renders, QUEUE_BLOCKS as u32);
    assert_eq!(p.write_block, 1);
    for _ in 0..p.renders {
        d.rendered();
    }
    assert_eq!(d.overruns(), 0);
    assert_eq!(d.queued_blocks(), QUEUE_BLOCKS as u32);
}

#[test]
fn each_block_completion_renders_one_more() {
    let mut d = primed();
    for block in 1..100 {
        let p = d.plan(frame_of(block), BLOCK_US);
        assert_eq!(p.renders, 1, "block {block}");
        assert_eq!(p.write_block, (block + QUEUE_BLOCKS) % RING_BLOCKS);
        d.rendered();
    }
    assert_eq!(d.overruns(), 0);
}

#[test]
fn a_wake_with_no_new_block_renders_nothing() {
    let mut d = primed();
    let p = d.plan(frame_of(0) + 5, 100);
    assert_eq!(p.renders, 0);
    assert!(!p.resynced);
}

#[test]
fn a_late_wake_catches_up_without_an_overrun_while_the_queue_lasts() {
    let mut d = primed();
    // Two blocks played before this wake: render two.
    let p = d.plan(frame_of(2), 2 * BLOCK_US);
    assert_eq!(p.renders, 2);
    assert!(!p.resynced);
    assert_eq!(p.skipped, 0);
    assert_eq!(d.overruns(), 0);
}

#[test]
fn reaching_an_unwritten_block_is_an_overrun_and_resyncs() {
    let mut d = primed();
    // Five blocks played: the codec passed the three queued ones.
    let p = d.plan(frame_of(5), 5 * BLOCK_US);
    assert!(p.resynced);
    assert_eq!(d.overruns(), 1);
    assert_eq!(p.write_block, 6 % RING_BLOCKS);
    assert_eq!(p.renders, QUEUE_BLOCKS as u32);
    // Blocks 4 and 5 were never written, and block 6 starts the new queue.
    assert_eq!(p.skipped, 2);
}

#[test]
fn a_stall_that_laps_the_ring_is_still_an_overrun() {
    let mut d = primed();
    // 215 ms, about nine laps: the position alone looks like block 2, inside the queue.
    let p = d.plan(frame_of(74), 74 * BLOCK_US);
    assert!(p.resynced);
    assert_eq!(p.skipped, 71);
    assert_eq!(d.overruns(), 1);
}

#[test]
fn a_whole_lap_measured_a_little_short_is_still_a_lap() {
    let mut d = primed();
    // Nine blocks played (one lap and one), but the timer saw a block less.
    let p = d.plan(frame_of(9), 8 * BLOCK_US);
    assert!(p.resynced);
    assert_eq!(d.overruns(), 1);
}

#[test]
fn restart_reanchors_the_queue_without_counting_an_overrun() {
    let mut d = primed();
    d.restart();
    // Long after: the codec is far past what was written, which a restart does not count.
    let p = d.plan(frame_of(4), 1_000_000);
    assert!(!p.resynced);
    assert_eq!(p.renders, QUEUE_BLOCKS as u32);
    assert_eq!(p.write_block, 5);
    assert_eq!(d.overruns(), 0);
}

#[test]
fn restart_keeps_the_timeline_running() {
    let mut d = primed();
    for block in 1..20 {
        let p = d.plan(frame_of(block), BLOCK_US);
        for _ in 0..p.renders {
            d.rendered();
        }
    }
    assert_eq!(d.played_block(), 19);
    d.restart();
    // Two blocks later: the play block keeps counting from where it was.
    d.plan(frame_of(21), 2 * BLOCK_US);
    assert_eq!(d.played_block(), 21);
    assert_eq!(d.written_blocks(), 22);
}

#[test]
fn the_timeline_counts_blocks_written_and_played() {
    let mut d = primed();
    assert_eq!((d.played_block(), d.written_blocks()), (0, 4));
    d.plan(frame_of(1), BLOCK_US);
    d.rendered();
    assert_eq!((d.played_block(), d.written_blocks()), (1, 5));
}

#[test]
fn the_codec_reaching_a_block_while_it_was_written_is_an_overrun() {
    let mut d = primed();
    // Five blocks late: a resync renders blocks 6, 7 and 8 while the codec plays block 5.
    let p = d.plan(frame_of(5), 5 * BLOCK_US);
    assert_eq!(d.overruns(), 1);
    d.rendered(); // block 6
    assert!(!d.check_written(frame_of(5) + 100));
    // The codec moved into block 7 while it was written.
    d.rendered(); // block 7
    assert!(d.check_written(frame_of(7) + 3));
    assert_eq!(d.overruns(), 2);
    d.rendered(); // block 8, after the codec: not counted again here
    assert!(!d.check_written(frame_of(7) + 60));
    assert_eq!(p.renders, 3);
    // The next plan sees the codec in block 7, which was written: no further overrun.
    let p = d.plan(frame_of(7) + 100, BLOCK_US);
    assert!(!p.resynced);
    assert_eq!(d.overruns(), 2);
}
