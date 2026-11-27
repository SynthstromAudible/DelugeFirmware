//! The fill tap's way to a sample's index: a table keyed by stream asset id.

use crate::PeakIndex;
use core::ptr::NonNull;
use core::sync::atomic::{AtomicPtr, Ordering};

/// As many asset ids as the fill-context table holds (`FILL_CONTEXT_CAP` in `deluge_storage_sample_fill`).
pub const ASSET_CAP: usize = 4096;

#[cfg_attr(target_os = "none", unsafe(link_section = ".sdram_bss"))]
static TABLE: [AtomicPtr<PeakIndex>; ASSET_CAP] =
    [const { AtomicPtr::new(core::ptr::null_mut()) }; ASSET_CAP];

/// Binds `index` to `asset`, taking a reference; releases any index bound before. Thread mode.
pub fn bind(asset: u32, index: &PeakIndex) {
    let Some(slot) = TABLE.get(asset as usize) else {
        return;
    };
    index.retain();
    let new = core::ptr::from_ref(index).cast_mut();
    let old = critical_section::with(|_| slot.swap(new, Ordering::AcqRel));
    if let Some(old) = NonNull::new(old) {
        PeakIndex::release(old);
    }
}

/// Unbinds `asset`, releasing its reference. Thread mode.
pub fn unbind(asset: u32) {
    let Some(slot) = TABLE.get(asset as usize) else {
        return;
    };
    let old = critical_section::with(|_| slot.swap(core::ptr::null_mut(), Ordering::AcqRel));
    if let Some(old) = NonNull::new(old) {
        PeakIndex::release(old);
    }
}

/// The index bound to `asset`, with a reference the caller must release. Any tier.
pub fn lookup(asset: u32) -> Option<NonNull<PeakIndex>> {
    let slot = TABLE.get(asset as usize)?;
    critical_section::with(|_| {
        let p = NonNull::new(slot.load(Ordering::Acquire))?;
        // SAFETY: a bound index holds the table's reference, and an unbind swaps it out only inside a critical
        // section, so it is alive here.
        unsafe { p.as_ref() }.retain();
        Some(p)
    })
}

/// One converted cluster's payload and the bytes of it that hold converted audio.
#[derive(Clone, Copy)]
pub struct ClusterView<'a> {
    /// The cluster's index in the file.
    pub index: u32,
    /// The payload: the cluster's bytes, then the slack copy of the next cluster's first bytes.
    pub payload: &'a [u8],
    /// The first byte offset in `payload` holding converted audio (a boundary word not yet converted is not).
    pub valid_from: u32,
    /// The end of the bytes holding converted audio: past the cluster's own bytes only when the slack holds the next
    /// cluster's converted head.
    pub valid_end: u32,
}

/// Where the audio sits in the file.
#[derive(Clone, Copy)]
pub struct ClusterGeometry {
    /// The byte the audio starts at.
    pub audio_start: u64,
    /// The audio's length in bytes.
    pub audio_length: u64,
    /// The cluster size, as a power of two.
    pub cluster_size_log2: u32,
    /// Bytes per sample, 1 to 4.
    pub byte_depth: u8,
    /// Channels per frame, 1 or 2.
    pub channels: u8,
}

/// The frames `[lo, hi)` whose bytes `view` holds, converted: a frame belongs to the cluster holding its first byte.
fn frames_of(geo: &ClusterGeometry, stride: u64, view: &ClusterView<'_>) -> (u64, u64) {
    let cluster_start = u64::from(view.index) << geo.cluster_size_log2;
    let cluster_end = cluster_start + (1u64 << geo.cluster_size_log2);
    let audio_end = geo.audio_start + geo.audio_length;
    let from = (cluster_start + u64::from(view.valid_from)).max(geo.audio_start);
    let bytes_end = (cluster_start + u64::from(view.valid_end)).min(audio_end);
    if bytes_end <= from {
        return (0, 0);
    }
    let lo = crate::div::ceil_div_u64(from - geo.audio_start, stride);
    // Frames starting in this cluster, and wholly within the valid bytes.
    let starts_end = crate::div::ceil_div_u64(cluster_end.min(audio_end) - geo.audio_start, stride);
    let whole_end = crate::div::floor_div_u64(bytes_end - geo.audio_start, stride);
    (lo, starts_end.min(whole_end).max(lo))
}

/// Folds every level-0 block not yet complete whose frames all lie in the contiguous run that `prev`, `this` and
/// `next` hold. A neighbour's blocks count too: this fill may have converted a boundary word the neighbour's own fill
/// could not.
pub fn fold_cluster(
    index: &PeakIndex,
    geo: &ClusterGeometry,
    prev: Option<ClusterView<'_>>,
    this: ClusterView<'_>,
    next: Option<ClusterView<'_>>,
) {
    let Some(stride8) = geo.byte_depth.checked_mul(geo.channels) else {
        return;
    };
    let stride = u64::from(stride8);
    if stride == 0 {
        return;
    }
    let views: [Option<ClusterView<'_>>; 3] = [prev, Some(this), next];
    let mut spans: [(u64, u64); 3] = [(0, 0); 3];
    for (n, v) in views.iter().enumerate() {
        if let Some(v) = v {
            spans[n] = frames_of(geo, stride, v);
        }
    }
    let (this_lo, this_hi) = spans[1];
    if this_hi <= this_lo {
        return;
    }
    // The contiguous run around `this`.
    let run_lo = if views[0].is_some() && spans[0].1 == this_lo && spans[0].0 < spans[0].1 {
        spans[0].0
    } else {
        this_lo
    };
    let run_hi = if views[2].is_some() && spans[2].0 == this_hi && spans[2].0 < spans[2].1 {
        spans[2].1
    } else {
        this_hi
    };
    let file_frames = crate::div::floor_div_u64(geo.audio_length, stride);
    let block_frames = 1u64 << crate::BLOCK_FRAMES_LOG2;
    let first_block = run_lo.div_ceil(block_frames);
    for b in first_block.. {
        let s = b * block_frames;
        let e = (s + block_frames).min(file_frames);
        if e > run_hi || e <= s {
            break;
        }
        if index.complete(0, b) {
            continue;
        }
        let mut peak = crate::Peak::EMPTY;
        let mut covered = 0u64;
        for (n, v) in views.iter().enumerate() {
            let Some(v) = v else { continue };
            let (lo, hi) = (spans[n].0.max(s), spans[n].1.min(e));
            if hi <= lo {
                continue;
            }
            let cluster_start = u64::from(v.index) << geo.cluster_size_log2;
            let from = geo.audio_start + lo * stride - cluster_start;
            let to = geo.audio_start + hi * stride - cluster_start;
            let (Ok(from), Ok(to)) = (usize::try_from(from), usize::try_from(to)) else {
                return;
            };
            let Some(bytes) = v.payload.get(from..to) else {
                return;
            };
            peak = peak.fold(crate::peak_of_frames(bytes, geo.byte_depth));
            covered += hi - lo;
        }
        if covered == e - s {
            index.fold_block(b, peak);
        }
    }
}
