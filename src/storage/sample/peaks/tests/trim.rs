//! Level 0 is kept only around the window reads asked for in the last second; trimmed chunks leave the index's
//! answers unchanged (levels 1 and up hold them).
use deluge_storage_sample_peaks::{KEEP_MS, Peak, PeakIndex, mem};

const CHUNK_FRAMES: u64 = 1 << 18; // 1,024 blocks of 256 frames

fn index(frames: u64) -> core::ptr::NonNull<PeakIndex> {
    mem::host::install();
    PeakIndex::create(frames, false, frames).expect("index")
}

#[test]
fn chunks_outside_the_kept_window_are_retired_and_freed_on_a_later_pass() {
    let p = index(8 * CHUNK_FRAMES);
    // SAFETY: released below.
    let i = unsafe { p.as_ref() };
    i.ensure_level0(0, 8 * CHUNK_FRAMES);
    assert_eq!(i.level0_chunks(), 8);
    i.keep_level0(3 * CHUNK_FRAMES + 10, 3 * CHUNK_FRAMES + 70_000, 1_000);
    assert_eq!(i.trim_level0(1_500), 7);
    assert_eq!(i.level0_chunks(), 1);
    // The window is older than KEEP_MS: everything goes.
    assert_eq!(i.trim_level0(1_000 + KEEP_MS + 1), 1);
    assert_eq!(i.level0_chunks(), 0);
    PeakIndex::release(p);
    PeakIndex::collect();
}

#[test]
fn windows_read_within_a_second_are_kept_together() {
    let p = index(8 * CHUNK_FRAMES);
    // SAFETY: released below.
    let i = unsafe { p.as_ref() };
    i.ensure_level0(0, 8 * CHUNK_FRAMES);
    i.keep_level0(0, 1000, 100);
    i.keep_level0(6 * CHUNK_FRAMES, 6 * CHUNK_FRAMES + 1000, 400);
    i.trim_level0(500);
    assert_eq!(i.level0_chunks(), 7); // chunks 0..=6: the union of both windows
    PeakIndex::release(p);
    PeakIndex::collect();
}

#[test]
fn a_fold_into_a_trimmed_chunk_still_completes_its_block() {
    let p = index(2 * CHUNK_FRAMES);
    // SAFETY: released below.
    let i = unsafe { p.as_ref() };
    i.ensure_level0(0, 2 * CHUNK_FRAMES);
    i.keep_level0(0, 10, 0);
    i.trim_level0(KEEP_MS + 1);
    i.fold_block(5, Peak { min: -3, max: 4 });
    assert!(i.block_complete(5));
    assert_eq!(i.block_peak(5), None); // no level-0 value kept
    PeakIndex::release(p);
    PeakIndex::collect();
}
