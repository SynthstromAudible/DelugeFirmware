//! A column's plan against brute force: every span, with and without kept level-0 values.

use deluge_storage_sample_peaks::{
    ColumnPlan, Peak, PeakIndex, column::resolve, mem, peak_of_frames,
};
use proptest::prelude::*;
use std::ptr::NonNull;

fn bytes_of(f: &[i16]) -> Vec<u8> {
    f.iter().flat_map(|v| v.to_le_bytes()).collect()
}

fn make(frames: u64) -> &'static PeakIndex {
    mem::host::install();
    // SAFETY: the index lives until `done` releases it and `collect` frees it.
    unsafe {
        PeakIndex::create(frames, false, frames)
            .expect("index")
            .as_ref()
    }
}

fn done(index: &PeakIndex) {
    PeakIndex::release(NonNull::from(index));
    PeakIndex::collect();
}

fn fold_all_but(idx: &PeakIndex, frames: &[i16], skip: Option<usize>) {
    let n = frames.len();
    for b in 0..n.div_ceil(256) {
        if Some(b) == skip {
            continue;
        }
        let (lo, hi) = (b * 256, ((b + 1) * 256).min(n));
        idx.fold_frames(lo as u64, &bytes_of(&frames[lo..hi]), 2, 1);
    }
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(128))]

    #[test]
    fn every_span_reads_brute_force(
        frames in prop::collection::vec(any::<i16>(), 1..70_000),
        a in any::<prop::sample::Index>(),
        len in 1usize..70_000,
        keep in 0u8..3,
    ) {
        let n = frames.len();
        let idx = make(n as u64);
        // 0: no level-0 values; 1: values kept from the start; 2: kept only after the folds (complete, valueless).
        if keep == 1 {
            idx.ensure_level0(0, n as u64);
        }
        fold_all_but(idx, &frames, None);
        if keep == 2 {
            idx.ensure_level0(0, n as u64);
        }
        let first = a.index(n);
        let end = (first + len).min(n);
        let plan = idx.column(first as u64, end as u64);
        if let ColumnPlan::Ready { reads, .. } = &plan {
            prop_assert!(reads.count <= 2);
            // Each of the (at most two) level-1 entries covered in part reads under 4,096 frames; two adjacent ones
            // merge into one range.
            let total: u64 = reads.ranges[..usize::from(reads.count)].iter().map(|(lo, hi)| hi - lo).sum();
            prop_assert!(total <= 2 * 4096, "edge reads are bounded: {}", total);
        }
        let got = resolve(plan, |lo, hi| Some(peak_of_frames(&bytes_of(&frames[lo as usize..hi as usize]), 2)));
        prop_assert_eq!(got, Some(peak_of_frames(&bytes_of(&frames[first..end]), 2)));
        done(idx);
    }

    #[test]
    fn an_unscanned_block_inside_makes_it_pending(frames in prop::collection::vec(any::<i16>(), 4_096..20_000), hole in 1usize..14) {
        let n = frames.len();
        let idx = make(n as u64);
        fold_all_but(idx, &frames, Some(hole));
        prop_assert_eq!(idx.column(0, n as u64), ColumnPlan::Pending);
        // A span that misses the hole still answers.
        let after = ((hole + 1) * 256) as u64;
        let ready = matches!(idx.column(after, n as u64), ColumnPlan::Ready { .. });
        prop_assert!(ready);
        done(idx);
    }
}

#[test]
fn kept_level0_values_leave_only_the_partial_blocks_to_read() {
    let frames: Vec<i16> = (0..8192).map(|i| (i % 1000) as i16).collect();
    let idx = make(frames.len() as u64);
    idx.ensure_level0(0, frames.len() as u64);
    fold_all_but(idx, &frames, None);
    // [100, 5000): block 0 in part, blocks 1..=18 whole, block 19 in part.
    let ColumnPlan::Ready { reads, .. } = idx.column(100, 5000) else {
        panic!("ready");
    };
    assert_eq!(reads.count, 2);
    assert_eq!(reads.ranges[0], (100, 256));
    assert_eq!(reads.ranges[1], (4864, 5000));
    done(idx);
}

#[test]
fn an_empty_span_is_ready_with_nothing() {
    let idx = make(1000);
    assert_eq!(
        idx.column(500, 500),
        ColumnPlan::Ready {
            interior: None,
            reads: Default::default()
        }
    );
    assert_eq!(
        resolve(idx.column(500, 500), |_, _| Some(Peak::EMPTY)),
        None
    );
    done(idx);
}
