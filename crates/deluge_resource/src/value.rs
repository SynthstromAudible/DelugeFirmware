//! Eviction policy: which resident chunk is evicted first when the pool needs space.
//!
//! [`evict_rank`] computes a rank compared lexicographically; the smallest is evicted first. Its
//! inputs, in order:
//!
//! - **relevance** — is the asset soft-referenced by the project? Referenced assets go last.
//! - **reconstruction cost per byte freed** — a big cheap chunk goes before a small dear one.
//! - **recency** — least-recently-used goes first.
//!
//! Leased and dirty chunks are never candidates; the caller filters them out.

// Reconstruction-cost ladder: ascending means dearer to rebuild, not "kept longer" — residency is
// decided by cost per byte (see `evict_rank`), so keep these pure rebuild expense. Plain `u32`
// rather than a C enum to avoid the `-fshort-enums` ABI trap.

/// Zero-fill or scratch memory, e.g. a grain buffer.
pub const COST_FREE: u32 = 0;
/// A native sample cluster: one storage read.
pub const COST_IO: u32 = 1;
/// A sample cluster needing format conversion: read plus re-convert.
pub const COST_IO_CONVERTED: u32 = 2;
/// A DSP recompute, e.g. a repitch cache or a wavetable band.
pub const COST_CPU: u32 = 3;
/// A perc cache: the heavier time-stretch recompute.
pub const COST_CPU_PERC: u32 = 4;
/// An `AudioFile` object: re-scan the header and recreate the descriptor (about a converted read).
///
/// The cost is modest on purpose; the object stays resident because it is tiny (high cost per
/// byte), not through an inflated cost. A held object is leased and never a candidate at all.
pub const COST_OBJECT: u32 = 2;

/// Fixed-point shift for the cost-per-byte ratio in [`evict_rank`].
///
/// Large enough that cost 1 over the largest realistic chunk keeps resolution; small enough that
/// `cost << SHIFT` stays well within `u64`.
const COST_PER_BYTE_SHIFT: u32 = 32;

/// Returns the lexicographic eviction rank `(referenced, cost_per_byte, recency)`; smaller is
/// evicted first.
///
/// Dividing `cost` by `size` separates how dear a chunk is to rebuild from how much memory evicting
/// it reclaims, so a tiny but pricey object sorts after the large cheap clusters. With uniform
/// sizes it reduces to plain `cost` order. `size == 0` is treated as 1, so such a chunk ranks high
/// and is evicted last.
#[inline]
pub fn evict_rank(soft_refs: u32, cost: u32, size: u32, recency: u64) -> (u8, u64, u64) {
    let referenced = u8::from(soft_refs > 0);
    let cost_per_byte = ((cost as u64) << COST_PER_BYTE_SHIFT) / (size.max(1) as u64);
    (referenced, cost_per_byte, recency)
}

#[cfg(test)]
mod tests {
    use super::*;

    const CLUSTER: u32 = 32 * 1024;

    #[test]
    fn unreferenced_evicted_before_referenced() {
        // referenced beats unreferenced regardless of cost/size/recency.
        assert!(evict_rank(0, COST_CPU, 1024, 1000) < evict_rank(1, COST_FREE, CLUSTER, 0));
    }

    #[test]
    fn cheaper_per_byte_evicted_first_then_lru() {
        // same reference state + same size: cheaper cost first, then older recency.
        assert!(evict_rank(0, COST_FREE, CLUSTER, 50) < evict_rank(0, COST_IO, CLUSTER, 0));
        assert!(evict_rank(1, COST_IO, CLUSTER, 10) < evict_rank(1, COST_IO, CLUSTER, 20));
    }

    #[test]
    fn uniform_size_reduces_to_cost_order() {
        // Equal sizes (the slab-cluster case): cost-per-byte preserves the plain cost ladder.
        assert!(evict_rank(0, COST_IO, CLUSTER, 0) < evict_rank(0, COST_IO_CONVERTED, CLUSTER, 0));
        assert!(evict_rank(0, COST_IO_CONVERTED, CLUSTER, 0) < evict_rank(0, COST_CPU, CLUSTER, 0));
    }

    #[test]
    fn big_cheap_freed_before_small_dear() {
        // The crux of size-awareness: a big chunk reclaims far more memory per unit rebuild-cost,
        // so it's evicted before a tiny dear one — even though its flat `cost` is higher.
        let big = evict_rank(0, COST_CPU_PERC, CLUSTER, 0); // dear recompute, but 32 KB
        let small = evict_rank(0, COST_OBJECT, 256, 0); // cheap-ish rescan, but a few hundred bytes
        assert!(
            big < small,
            "big chunk frees more per rebuild-cost ⇒ evict it first"
        );
    }

    #[test]
    fn zero_size_sorts_last_not_panics() {
        // size 0 must not divide-by-zero; it frees nothing ⇒ ranks high (evicted last).
        assert!(evict_rank(0, COST_IO, CLUSTER, 0) < evict_rank(0, COST_IO, 0, 0));
    }
}
