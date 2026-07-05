//! Eviction value: the policy that decides which resident chunk dies first when
//! the pool needs space. A computed *rank* (not a hand-assigned category — see
//! docs/dev/resource_manager.md), compared lexicographically; the **smallest rank
//! is evicted first**.
//!
//! Inputs (replacing the old 10 `StealableQueue`s):
//!   - **relevance** — is the asset soft-referenced by the project? (kept last)
//!   - **reconstruction cost per byte freed** — evicting a big cheap chunk reclaims
//!     more memory for less rebuild than a small dear one, so it dies first
//!   - **recency** — least-recently-used dies first (tiebreak)
//!
//! Leased / dirty chunks are never candidates — that's enforced by the caller, so
//! it isn't part of the rank.

/// Reconstruction-cost ladder — ascending = dearer to *rebuild* (NOT "kept longer"; how long a chunk
/// stays resident is `cost / size`, see `evict_rank`). Plain `u32` so it crosses the C ABI without a C
/// `enum` (avoids the `-fshort-enums` ABI trap). Replaces the old hand-assigned 10 `StealableQueue`s;
/// the value function only compares it numerically, so the ladder is just a sensible ordering of real
/// rebuild expense. Keep these *pure rebuild-expense* — memory-yield is the value function's job (size).
pub const COST_FREE: u32 = 0; // zero-fill / scratch (cheapest; e.g. a grain buffer)
pub const COST_IO: u32 = 1; // native sample cluster — one storage read
pub const COST_IO_CONVERTED: u32 = 2; // sample cluster needing format conversion — read + re-convert
pub const COST_CPU: u32 = 3; // DSP recompute — a repitch cache / wavetable band
pub const COST_CPU_PERC: u32 = 4; // perc cache — the heavier time-stretch recompute
pub const COST_OBJECT: u32 = 2; // an AudioFile object — re-scan the file header + recreate the descriptor
                                // (read + parse ≈ a converted read). It's *cost* that's modest; the
                                // object stays resident because it's TINY (high cost/byte), via the value
                                // function's size term — not via an inflated cost. NB: a *held* object is
                                // hard-leased (never a candidate); this only orders DEAD (unleased) chunks.

/// Fixed-point shift for the cost-per-byte ratio (see `evict_rank`). Large enough that the cheapest
/// cost (1) over the largest realistic chunk (a 32 KB cluster, and bigger caches) keeps bits of
/// resolution to rank against dearer/smaller chunks; small enough that `cost << SHIFT` stays well
/// within `u64` (max cost · 2^32 ≪ 2^64).
const COST_PER_BYTE_SHIFT: u32 = 32;

/// Lexicographic evict rank; smaller is evicted first.
/// `(referenced, cost_per_byte, recency)` → evict un-referenced before referenced, then the lowest
/// **reconstruction-cost-per-byte-freed**, then least-recently-used. Dividing `cost` by `size`
/// separates the two things the old flat `cost` conflated — how dear a chunk is to *rebuild* vs. how
/// much memory evicting it *reclaims* — so a tiny-but-pricey object (an AudioFile descriptor) sorts
/// *after* the fat cheap clusters it would otherwise tie with, with no inflated cost. With uniform
/// sizes (the cluster case) it reduces to the plain `cost` ordering. `size == 0` is treated as 1 (a
/// degenerate chunk frees nothing → ranks high → evicted last; never divides by zero).
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
        // The crux of size-awareness: a big chunk reclaims far more memory per unit rebuild-cost, so
        // it's evicted before a tiny dear one — even though its flat `cost` is higher.
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
