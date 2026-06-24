//! Eviction value: the policy that decides which resident chunk dies first when
//! the pool needs space. A computed *rank* (not a hand-assigned category — see
//! docs/dev/resource_manager.md), compared lexicographically; the **smallest rank
//! is evicted first**.
//!
//! Inputs (replacing the old 10 `StealableQueue`s):
//!   - **relevance** — is the asset soft-referenced by the project? (kept last)
//!   - **reconstruction cost** — cheaper-to-rebuild dies first (the ladder below)
//!   - **recency** — least-recently-used dies first (tiebreak)
//!
//! Leased / dirty chunks are never candidates — that's enforced by the caller, so
//! it isn't part of the rank.

/// Reconstruction-cost ladder — ascending = dearer to rebuild = kept resident longer (evicted later).
/// Plain `u32` so it crosses the C ABI without a C `enum` (avoids the `-fshort-enums` ABI trap). A
/// computed score replacing the old hand-assigned 10 `StealableQueue`s; the value function only
/// compares it numerically, so the ladder is just a sensible ordering of real rebuild expense.
pub const COST_FREE: u32 = 0; // zero-fill / scratch (cheapest; e.g. a grain buffer)
pub const COST_IO: u32 = 1; // native sample cluster — one storage read
pub const COST_IO_CONVERTED: u32 = 2; // sample cluster needing format conversion — read + re-convert
pub const COST_CPU: u32 = 3; // DSP recompute — a repitch cache / wavetable band
pub const COST_CPU_PERC: u32 = 4; // perc cache — the heavier time-stretch recompute
pub const COST_OBJECT: u32 = 5; // an AudioFile object — tiny (freeing it reclaims ~nothing) yet a full
                                // file re-scan to rebuild, and its big clusters/caches are evictable
                                // individually first, so reclaim the descriptor last. NB: a *held* object
                                // is hard-leased (never a candidate); this cost only orders DEAD (unleased)
                                // objects vs other dead chunks — relevance is handled by the lease, not here.

/// Lexicographic evict rank; smaller is evicted first.
/// `(referenced, cost, recency)` → evict un-referenced before referenced, then
/// cheaper-to-rebuild before dearer, then least-recently-used before recent.
#[inline]
pub fn evict_rank(soft_refs: u32, cost: u32, recency: u64) -> (u8, u32, u64) {
    let referenced = u8::from(soft_refs > 0);
    (referenced, cost, recency)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unreferenced_evicted_before_referenced() {
        // referenced beats unreferenced regardless of cost/recency.
        assert!(evict_rank(0, COST_CPU, 1000) < evict_rank(1, COST_FREE, 0));
    }

    #[test]
    fn cheaper_evicted_first_then_lru() {
        // same reference state: cheaper cost first, then older recency.
        assert!(evict_rank(0, COST_FREE, 50) < evict_rank(0, COST_IO, 0));
        assert!(evict_rank(1, COST_IO, 10) < evict_rank(1, COST_IO, 20));
    }
}
