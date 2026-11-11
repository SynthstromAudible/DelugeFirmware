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
}

#[test]
fn a_stall_that_laps_the_ring_is_still_an_overrun() {
    let mut d = primed();
    // 215 ms, about nine laps: the position alone looks like block 2, inside the queue.
    let p = d.plan(frame_of(74), 74 * BLOCK_US);
    assert!(p.resynced);
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
fn restart_forgets_the_old_position() {
    let mut d = primed();
    d.restart();
    let p = d.plan(frame_of(4), 1_000_000);
    assert!(!p.resynced);
    assert_eq!(p.renders, QUEUE_BLOCKS as u32);
    assert_eq!(p.write_block, 5);
    assert_eq!(d.overruns(), 0);
}
