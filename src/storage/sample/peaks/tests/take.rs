//! A take's peaks folded cluster by cluster, against brute force over the same bytes.

use deluge_storage_sample_peaks::take::Take;
use deluge_storage_sample_peaks::{ColumnPlan, Peak, PeakIndex, mem};
use proptest::prelude::*;
use std::ptr::NonNull;

/// A recorded file: `header` bytes, then `frames` 24-bit frames of `channels` channels, pseudo-random.
fn file_of(header: usize, frames: usize, channels: usize, seed: u64) -> Vec<u8> {
    let mut f = vec![0xee; header + frames * 3 * channels];
    let mut s = seed | 1;
    for b in f.iter_mut().skip(header) {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        *b = s as u8;
    }
    f
}

fn brute(f: &[u8], header: usize, channels: usize, first: u64, end: u64) -> Peak {
    let mut p = Peak::EMPTY;
    for i in first..end {
        for c in 0..channels {
            let off = header + (i as usize * channels + c) * 3;
            let v = i16::from_le_bytes([f[off + 1], f[off + 2]]);
            p = p.fold(Peak { min: v, max: v });
        }
    }
    p
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(64))]

    #[test]
    fn every_block_matches_brute_force(
        header in prop::sample::select(vec![44usize, 112]),
        frames in 1usize..6_000,
        channels in 1usize..=2,
        pieces in prop::collection::vec(1usize..3_000, 1..8),
    ) {
        mem::host::install();
        let f = file_of(header, frames, channels, frames as u64 * 31 + header as u64);
        // SAFETY: released and collected at the end.
        let idx = unsafe { PeakIndex::create(0, true, 1 << 24).expect("index").as_ref() };
        idx.ensure_level0(0, frames as u64);
        let mut take = Take::default();
        let mut at = 0;
        for n in 0.. {
            if at >= f.len() {
                break;
            }
            let len = pieces[n % pieces.len()].min(f.len() - at);
            take.feed(Some(idx), &f[at..at + len], header as u32, channels as u8);
            at += len;
        }
        idx.set_frames(frames as u64, true);
        take.finish(Some(idx));
        prop_assert_eq!(take.frames(), frames as u64);
        prop_assert_eq!(idx.next_unscanned(0), None);
        for b in 0..(frames as u64).div_ceil(256) {
            let end = ((b + 1) * 256).min(frames as u64);
            let ColumnPlan::Ready { interior, reads } = idx.column(b * 256, end) else {
                return Err(TestCaseError::fail(format!("block {b} pending")));
            };
            prop_assert_eq!(reads.count, 0);
            prop_assert_eq!(interior, Some(brute(&f, header, channels, b * 256, end)));
        }
        PeakIndex::release(NonNull::from(idx));
        PeakIndex::collect();
    }
}

#[test]
fn folds_the_last_part_filled_block_only_when_finished() {
    mem::host::install();
    let f = file_of(44, 300, 1, 9);
    // SAFETY: released and collected at the end.
    let idx = unsafe { PeakIndex::create(0, true, 4096).expect("index").as_ref() };
    let mut take = Take::default();
    take.feed(Some(idx), &f, 44, 1);
    idx.set_frames(300, false);
    assert_eq!(idx.next_unscanned(0), Some(256));
    idx.set_frames(300, true);
    take.finish(Some(idx));
    assert_eq!(idx.next_unscanned(0), None);
    assert_eq!(take.frames(), 300);
    PeakIndex::release(NonNull::from(idx));
    PeakIndex::collect();
}

#[test]
fn reserves_its_own_room() {
    mem::host::install();
    let f = file_of(44, 70_000, 1, 3);
    // SAFETY: released and collected at the end.
    let idx = unsafe { PeakIndex::create(0, true, 1 << 24).expect("index").as_ref() };
    let mut take = Take::default();
    take.feed(Some(idx), &f, 44, 1);
    idx.set_frames(70_000, true);
    take.finish(Some(idx));
    // Nothing reserved the index but the feed: every block still landed.
    assert_eq!(idx.next_unscanned(0), None);
    PeakIndex::release(NonNull::from(idx));
    PeakIndex::collect();
}
