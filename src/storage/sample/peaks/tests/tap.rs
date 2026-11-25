//! The fill tap against brute force: clusters filled in a random order, each with whichever neighbours filled before
//! it, every byte outside a view's valid range garbled.

use deluge_storage_sample_peaks::tap::{ClusterGeometry, ClusterView, fold_cluster};
use deluge_storage_sample_peaks::{BLOCK_FRAMES_LOG2, PeakIndex, mem, peak_of_frames};
use proptest::prelude::*;
use std::ptr::NonNull;

const SLACK: usize = 7;
/// Full scale, where the audio is quiet: a garbled byte read as audio moves its block's peak.
const GARBAGE: u8 = 0x7f;

struct File {
    bytes: Vec<u8>,
    start: u64,
    frames: u64,
    log2: u32,
    depth: u8,
    channels: u8,
}

impl File {
    fn new(start: u64, log2: u32, depth: u8, channels: u8, clusters: usize, seed: u64) -> Self {
        let cs = 1usize << log2;
        let stride = usize::from(depth) * usize::from(channels);
        let total = cs * clusters;
        let frames = ((total - start as usize) / stride) as u64;
        let mut bytes = vec![GARBAGE; total];
        let mut s = seed | 1;
        // Quiet audio: each sample's top byte within ±16.
        let depth_us = usize::from(depth);
        for (i, b) in bytes
            .iter_mut()
            .skip(start as usize)
            .take(frames as usize * stride)
            .enumerate()
        {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            *b = if i % depth_us == depth_us - 1 {
                ((s % 33) as i8 - 16) as u8
            } else {
                s as u8
            };
        }
        File {
            bytes,
            start,
            frames,
            log2,
            depth,
            channels,
        }
    }

    fn geometry(&self) -> ClusterGeometry {
        ClusterGeometry {
            audio_start: self.start,
            audio_length: self.frames * self.stride(),
            cluster_size_log2: self.log2,
            byte_depth: self.depth,
            channels: self.channels,
        }
    }

    fn stride(&self) -> u64 {
        u64::from(self.depth) * u64::from(self.channels)
    }

    fn clusters(&self) -> usize {
        self.bytes.len() >> self.log2
    }

    /// Cluster `c`'s payload with everything outside `[from, end)` garbled.
    fn payload(&self, c: usize, from: usize, end: usize) -> Vec<u8> {
        let cs = 1usize << self.log2;
        let mut p: Vec<u8> = (0..cs + SLACK)
            .map(|i| *self.bytes.get(c * cs + i).unwrap_or(&GARBAGE))
            .collect();
        for (i, b) in p.iter_mut().enumerate() {
            if i < from || i >= end {
                *b = GARBAGE;
            }
        }
        p
    }

    fn brute_block(&self, b: u64) -> deluge_storage_sample_peaks::Peak {
        let s = b << BLOCK_FRAMES_LOG2;
        let e = (s + (1 << BLOCK_FRAMES_LOG2)).min(self.frames);
        let lo = (self.start + s * self.stride()) as usize;
        let hi = (self.start + e * self.stride()) as usize;
        peak_of_frames(&self.bytes[lo..hi], self.depth)
    }
}

/// One cluster's convert state in the simulation: whether its head word and its tail (with the slack) are converted.
#[derive(Clone, Copy, Default)]
struct State {
    filled: bool,
    head_ok: bool,
    tail_ok: bool,
}

proptest! {
    #![proptest_config(ProptestConfig::with_cases(96))]

    #[test]
    fn folds_exactly_the_blocks_its_clusters_hold(
        start in 0u64..200, log2 in 11u32..=15, depth in 1u8..=4, channels in 1u8..=2,
        clusters in 2usize..12, seed in any::<u64>(), native in any::<bool>(), fill_all in any::<bool>(),
    ) {
        mem::host::install();
        let f = File::new(start, log2, depth, channels, clusters, seed);
        prop_assume!(f.frames > 0);
        let cs = 1usize << log2;
        // SAFETY: released and collected at the end.
        let idx = unsafe { PeakIndex::create(f.frames, false, f.frames).expect("index").as_ref() };
        idx.ensure_level0(0, f.frames);
        // A shuffled fill order over a random subset (or all) of the clusters.
        let mut order: Vec<usize> = (0..f.clusters()).collect();
        let mut s = seed | 1;
        for i in (1..order.len()).rev() {
            s ^= s << 13; s ^= s >> 7; s ^= s << 17;
            order.swap(i, (s % (i as u64 + 1)) as usize);
        }
        if !fill_all {
            order.truncate(1 + (s as usize % order.len()));
        }
        let mut st = vec![State::default(); f.clusters()];
        // A cluster's own boundary words convert only with its neighbour present (a native file needs no convert).
        let head_from = |ok: bool| if native || ok { 0 } else { 4 };
        for &c in &order {
            let has_prev = c > 0 && st[c - 1].filled;
            let has_next = c + 1 < f.clusters() && st[c + 1].filled;
            // The first cluster has no head word to wait for, and the last no tail word.
            st[c] = State { filled: true, head_ok: has_prev || c == 0, tail_ok: has_next || c + 1 == f.clusters() };
            if has_prev { st[c - 1].tail_ok = true; }
            if has_next { st[c + 1].head_ok = true; }
            let end_of = |ok: bool| if ok { cs + SLACK } else if native { cs } else { cs - 4 };
            let p_this = f.payload(c, head_from(st[c].head_ok), end_of(st[c].tail_ok));
            let this = ClusterView { index: c as u32, payload: &p_this, valid_from: head_from(st[c].head_ok) as u32, valid_end: end_of(st[c].tail_ok) as u32 };
            let p_prev = has_prev.then(|| f.payload(c - 1, head_from(st[c - 1].head_ok), cs + SLACK));
            let prev = p_prev.as_ref().map(|p| ClusterView { index: (c - 1) as u32, payload: p, valid_from: head_from(st[c - 1].head_ok) as u32, valid_end: (cs + SLACK) as u32 });
            let p_next = has_next.then(|| f.payload(c + 1, 0, end_of(st[c + 1].tail_ok)));
            let next = p_next.as_ref().map(|p| ClusterView { index: (c + 1) as u32, payload: p, valid_from: 0, valid_end: end_of(st[c + 1].tail_ok) as u32 });
            fold_cluster(idx, &f.geometry(), prev, this, next);
        }
        let blocks = f.frames.div_ceil(1 << BLOCK_FRAMES_LOG2);
        for b in 0..blocks {
            if idx.block_complete(b) {
                prop_assert_eq!(idx.block_peak(b), Some(f.brute_block(b)), "block {}", b);
            }
        }
        if fill_all {
            // Every cluster filled, each with the neighbours filled before it: every block is complete.
            prop_assert_eq!(idx.next_unscanned(0), None);
        }
        PeakIndex::release(NonNull::from(idx));
        PeakIndex::collect();
    }
}

#[test]
fn a_head_word_not_yet_converted_is_not_read() {
    // Cluster 1 fills before cluster 0 in a non-native file: its first bytes are unconverted until cluster 0's stitch,
    // and block 4 starts exactly at cluster 1's first byte (2,048-byte clusters of 16-bit mono: 4 blocks each).
    mem::host::install();
    let f = File::new(0, 11, 2, 1, 2, 7);
    let cs = 1usize << 11;
    // SAFETY: released and collected at the end.
    let idx = unsafe {
        PeakIndex::create(f.frames, false, f.frames)
            .expect("index")
            .as_ref()
    };
    idx.ensure_level0(0, f.frames);
    let p1 = f.payload(1, 4, cs);
    let only = ClusterView {
        index: 1,
        payload: &p1,
        valid_from: 4,
        valid_end: cs as u32,
    };
    fold_cluster(idx, &f.geometry(), None, only, None);
    assert!(
        !idx.block_complete(4),
        "block 4 needs cluster 1's head word"
    );
    // Cluster 0 arrives: its stitch converts cluster 1's head, and block 4 completes with the true peak.
    let p0 = f.payload(0, 0, cs + SLACK);
    let p1 = f.payload(1, 0, cs);
    let this = ClusterView {
        index: 0,
        payload: &p0,
        valid_from: 0,
        valid_end: (cs + SLACK) as u32,
    };
    let next = ClusterView {
        index: 1,
        payload: &p1,
        valid_from: 0,
        valid_end: cs as u32,
    };
    fold_cluster(idx, &f.geometry(), None, this, Some(next));
    assert_eq!(idx.block_peak(4), Some(f.brute_block(4)));
    assert_eq!(idx.next_unscanned(0), None);
    PeakIndex::release(NonNull::from(idx));
    PeakIndex::collect();
}
