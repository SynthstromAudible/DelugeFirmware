//! The encoding, the exact division and the pyramid's folds against brute force.

use deluge_storage_sample_peaks::div::{ceil_div_u64, floor_div_u64, frames_in_bytes};
use deluge_storage_sample_peaks::{
    BLOCK_FRAMES_LOG2, ColumnPlan, Peak, PeakIndex, column::resolve, mem, peak_of_frames,
};
use proptest::prelude::*;
use std::ptr::NonNull;

/// Mono 16-bit frames as bytes.
fn bytes_of(frames: &[i16]) -> Vec<u8> {
    frames.iter().flat_map(|v| v.to_le_bytes()).collect()
}

fn make(frames: u64, growing: bool, capacity: u64) -> &'static PeakIndex {
    mem::host::install();
    // SAFETY: the index lives until `done` releases it and `collect` frees it.
    unsafe {
        PeakIndex::create(frames, growing, capacity)
            .expect("index")
            .as_ref()
    }
}

fn done(index: &PeakIndex) {
    PeakIndex::release(NonNull::from(index));
    PeakIndex::collect();
}

proptest! {
    #[test]
    fn encode_round_trips(min in any::<i16>(), max in any::<i16>()) {
        let p = Peak { min, max };
        prop_assert_eq!(Peak::decode(p.encode()), p);
    }

    #[test]
    fn fold_is_min_and_max(a in any::<(i16, i16)>(), b in any::<(i16, i16)>()) {
        let pa = Peak { min: a.0, max: a.1 };
        let pb = Peak { min: b.0, max: b.1 };
        let f = pa.fold(pb);
        prop_assert_eq!(f, Peak { min: a.0.min(b.0), max: a.1.max(b.1) });
        prop_assert_eq!(Peak::decode(Peak::encode_fold(pa.encode(), pb.encode())), f);
    }

    #[test]
    fn divides_exactly(n in 0u64..(1u64 << 53), d in 1u64..=8) {
        prop_assert_eq!(floor_div_u64(n, d), n / d);
        prop_assert_eq!(ceil_div_u64(n, d), n.div_ceil(d));
    }

    #[test]
    fn whole_frames(bytes in 0u32..(1 << 20), stride in prop::sample::select(vec![1u8, 2, 3, 4, 6, 8])) {
        prop_assert_eq!(frames_in_bytes(bytes, stride), bytes / u32::from(stride));
    }
}

#[test]
fn zero_is_empty() {
    assert!(Peak::decode(0).is_empty());
    assert_eq!(Peak::EMPTY.encode(), 0);
}

#[test]
fn reads_the_top_sixteen_bits_at_every_depth() {
    assert_eq!(
        peak_of_frames(&[0x80, 0x7f], 1),
        Peak {
            min: -32768,
            max: 0x7f00
        }
    );
    assert_eq!(
        peak_of_frames(&[0x00, 0x80, 0xff, 0x7f], 2),
        Peak {
            min: -32768,
            max: 32767
        }
    );
    assert_eq!(
        peak_of_frames(&[0xaa, 0x34, 0x12], 3),
        Peak {
            min: 0x1234,
            max: 0x1234
        }
    );
    assert_eq!(
        peak_of_frames(&[0xaa, 0xbb, 0x00, 0x80], 4),
        Peak {
            min: -32768,
            max: -32768
        }
    );
    assert!(peak_of_frames(&[], 2).is_empty());
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(64))]

    #[test]
    fn folds_in_any_order_match_brute_force(
        frames in prop::collection::vec(any::<i16>(), 1..20_000),
        cuts in prop::collection::vec(any::<prop::sample::Index>(), 0..40),
        order_seed in any::<u64>(),
    ) {
        let n = frames.len() as u64;
        let idx = make(n, false, n);
        // Random spans covering the whole file, folded in a shuffled order.
        let mut edges: Vec<usize> = cuts.iter().map(|c| c.index(frames.len())).collect();
        edges.push(0);
        edges.push(frames.len());
        edges.sort_unstable();
        edges.dedup();
        let mut spans: Vec<(usize, usize)> = edges.windows(2).map(|w| (w[0], w[1])).collect();
        let mut s = order_seed | 1;
        for i in (1..spans.len()).rev() {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            spans.swap(i, (s % (i as u64 + 1)) as usize);
        }
        for (a, b) in &spans {
            idx.fold_frames(*a as u64, &bytes_of(&frames[*a..*b]), 2, 1);
        }
        // fold_frames completes only blocks wholly inside one call; finish the rest block by block, as the scan does.
        let block = 1usize << BLOCK_FRAMES_LOG2;
        for b in 0..frames.len().div_ceil(block) {
            let (lo, hi) = (b * block, ((b + 1) * block).min(frames.len()));
            idx.fold_frames(lo as u64, &bytes_of(&frames[lo..hi]), 2, 1);
        }
        prop_assert_eq!(idx.range(), Some(peak_of_frames(&bytes_of(&frames), 2)));
        prop_assert_eq!(idx.next_unscanned(0), None);
        for (a, b) in [(0u64, n), (0, n / 2), (n / 3, n), (n / 5, n / 5 + n / 7 + 1)] {
            let b = b.min(n);
            if b <= a {
                continue;
            }
            let got = resolve(idx.column(a, b), |lo, hi| {
                Some(peak_of_frames(&bytes_of(&frames[lo as usize..hi as usize]), 2))
            });
            prop_assert_eq!(got, Some(peak_of_frames(&bytes_of(&frames[a as usize..b as usize]), 2)));
        }
        done(idx);
    }

    #[test]
    fn partial_folds_never_complete_a_block(frames in prop::collection::vec(any::<i16>(), 512..4_000), cut in 1usize..255) {
        let n = frames.len() as u64;
        let idx = make(n, false, n);
        idx.fold_frames(0, &bytes_of(&frames[..cut]), 2, 1);
        prop_assert_eq!(idx.next_unscanned(0), Some(0));
        prop_assert_eq!(idx.range(), None);
        prop_assert_eq!(idx.column(0, 256), ColumnPlan::Pending);
        done(idx);
    }

    #[test]
    fn a_growing_index_completes_its_tail_only_when_final(extra in 1u64..255) {
        let full = 16u64 << BLOCK_FRAMES_LOG2; // one level-1 entry's frames
        let idx = make(0, true, 1 << 24);
        idx.reserve(full + extra);
        let frames: Vec<i16> = (0..(full + extra) as i16).collect();
        idx.set_frames(full + extra, false);
        idx.fold_frames(0, &bytes_of(&frames), 2, 1);
        // The 16 whole blocks complete; the tail block is not complete while the take grows.
        prop_assert_eq!(idx.next_unscanned(0), Some(full));
        idx.set_frames(full + extra, true);
        idx.fold_frames(full, &bytes_of(&frames[full as usize..]), 2, 1);
        prop_assert_eq!(idx.next_unscanned(0), None);
        prop_assert_eq!(idx.range(), Some(Peak { min: 0, max: (full + extra - 1) as i16 }));
        done(idx);
    }
}

#[test]
fn a_fold_past_a_takes_reserve_is_dropped() {
    let idx = make(0, true, 1 << 24);
    idx.reserve(256);
    // One level-1 chunk is 1,024 entries of 4,096 frames: 2^22 frames. Fold one level-1 entry past it.
    let reserved_frames = 1u64 << (BLOCK_FRAMES_LOG2 + 4 + 10);
    let frames: Vec<i16> = vec![5; (reserved_frames + 4096) as usize];
    idx.set_frames(reserved_frames + 4096, false);
    idx.fold_frames(0, &bytes_of(&frames), 2, 1);
    // Every block within the reserve is complete; the first past it is not.
    assert_eq!(idx.next_unscanned(0), Some(reserved_frames));
    done(idx);
}

#[test]
fn a_released_index_is_freed_only_by_collect_after_its_last_reference() {
    let idx = make(1000, false, 1000);
    idx.retain();
    PeakIndex::release(NonNull::from(idx));
    // Still referenced: usable.
    idx.fold_frames(0, &bytes_of(&[1; 256]), 2, 1);
    assert_eq!(idx.range(), Some(Peak { min: 1, max: 1 }));
    done(idx);
}

#[test]
fn divides_exactly_where_a_double_quotient_rounds() {
    // Near 2^53 a double quotient of `d·k − 1` by `d` rounds up to `k`; the correction step brings it back.
    for d in 1u64..=8 {
        for n in [
            (1u64 << 53) - 1,
            (1u64 << 53) - 2,
            (1u64 << 52) - 1,
            (1u64 << 53) - d,
            (1u64 << 53) - d - 1,
        ] {
            assert_eq!(floor_div_u64(n, d), n / d, "{n} / {d}");
            assert_eq!(ceil_div_u64(n, d), n.div_ceil(d), "{n} / {d} rounded up");
        }
    }
}

#[test]
fn a_complete_block_folded_again_fills_only_its_kept_value() {
    let n = 1u64 << 16; // one level-2 entry's frames
    let idx = make(n, false, n);
    idx.fold_frames(0, &bytes_of(&vec![5; n as usize]), 2, 1);
    idx.ensure_level0(0, 256);
    // Another peak for block 0: the levels above and the range have block 0 already, and keep it.
    idx.fold_block(
        0,
        Peak {
            min: -100,
            max: 100,
        },
    );
    assert_eq!(idx.range(), Some(Peak { min: 5, max: 5 }));
    for (a, b) in [(0u64, 4096u64), (0, n)] {
        let got = resolve(idx.column(a, b), |_, _| Some(Peak { min: 5, max: 5 }));
        assert_eq!(got, Some(Peak { min: 5, max: 5 }), "column [{a}, {b})");
    }
    // The kept value is filled from the fold.
    assert_eq!(
        idx.block_peak(0),
        Some(Peak {
            min: -100,
            max: 100
        })
    );
    done(idx);
}

#[test]
fn a_fold_past_a_final_index_end_is_dropped() {
    let n = 3 * 256 + 10;
    let idx = make(n, false, n);
    idx.fold_block(20, Peak { min: -9, max: 9 }); // past the end, inside the last level-1 entry's children
    idx.fold_frames(0, &bytes_of(&vec![3; n as usize]), 2, 1);
    assert_eq!(idx.next_unscanned(0), None);
    assert_eq!(idx.range(), Some(Peak { min: 3, max: 3 }));
    assert!(matches!(idx.column(0, n), ColumnPlan::Ready { .. }));
    done(idx);
}
