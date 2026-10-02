#pragma once

// Published<T> -- the "publish spine" primitive for the audio snapshot boundary: an
// immutable snapshot handed off via atomic pointer-swap, with an app-owned retire list
// for safe, race-free reclamation once the reader can no longer see the old snapshot.
// See docs/dev/target_architecture.md §4.2 (mechanism 1) for the architectural slot this fills.
//
// Concurrency contract: SPSC-shaped. Exactly one [audio] reader thread calls load();
// exactly one [task] writer thread calls publish()/reclaim(). No multi-writer, no
// multi-reader support -- callers may rely on that; it is not re-checked at runtime
// (no <thread> dependency is taken here so this header stays compilable on the bare-
// metal/no-OS device build, not just the host sim).

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace deluge::util {

/// @brief Shared block-epoch source for one "audio domain" -- every `Published<T>`
///        that lives on the same audio render path should reference the same
///        `SnapshotDomain`, so the [audio] executor only needs to bump one counter
///        per render block regardless of how many `Published<T>` instances it reads.
///
/// @par Memory ordering
/// `advance_epoch()` is called by [audio] exactly once, at the *start* of each render
/// block, before that block's `load()` calls. It is written as a plain (non-atomic)
/// self-read of the previous value followed by a **release** store -- not a
/// `fetch_add`, so there is no read-modify-write on the reader side (only [audio]
/// ever writes this counter, so the relaxed self-read races with nothing). The
/// release store is what makes the two-grace-period argument in `Published<T>`
/// correct rather than merely "true in practice on typical hardware": because the
/// store sits at the *top* of block E, everything [audio] did in program order
/// during block E-1 and earlier (including every non-atomic read of a snapshot
/// obtained from a prior `load()`) is sequenced-before this store. A [task] thread
/// that later reads the counter with `audio_epoch()` (acquire) and observes a value
/// >= E therefore *synchronizes-with* that release store, which establishes a real
/// happens-before edge from "[audio] finished reading whatever it held during block
/// E-1" to "[task] observed epoch >= E" -- not just a temporal coincidence. This is
/// what lets `Published<T>::reclaim()` free retired memory without racing the reader,
/// in the formal C++ memory-model sense -- and is why the epoch counter deliberately
/// uses release, not relaxed, store semantics: a relaxed-only store would leave
/// `reclaim()`'s `free()` undefined behavior, since a temporal/wall-clock argument
/// alone is not a happens-before edge.
class SnapshotDomain {
public:
	// [audio] must be able to advance/observe this epoch without ever taking a lock;
	// fail to compile rather than silently blocking the render thread on a target
	// where std::atomic<uint64_t> isn't always lock-free.
	static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

	/// @brief [audio] Advance the block epoch by one. Call exactly once per render
	///        block, before any `Published<T>::load()` for that block.
	void advance_epoch() noexcept {
		// Only-writer self-read: no other thread ever stores to epoch_, so a relaxed
		// read of "what I last wrote" cannot race. The store that follows is the
		// operation that must carry release semantics (see class comment).
		const std::uint64_t next = epoch_.load(std::memory_order_relaxed) + 1;
		epoch_.store(next, std::memory_order_release);
		// Store-buffering fix, paired with the seq_cst fence in `RetireList::retire()` right before its
		// `audio_epoch()` read: acquire/release alone only orders accesses to epoch_ itself, not this store
		// against an independent atomic on the other thread (the retired object's unlink). That leaves the SB
		// litmus open -- [audio]'s release store here and [task]'s unlink-then-acquire-load in `retire()` can
		// each observe the other's write as not-yet-happened, so `retire()` at epoch 0 could free an object
		// this render block has already loaded. This seq_cst fence, together with the matching one in
		// `retire()`, forces a single total order between "epoch is now visible" and "object is now unlinked",
		// closing that window.
		std::atomic_thread_fence(std::memory_order_seq_cst);
	}

	/// @brief [task] Observe the current audio-block epoch. Acquire ordering:
	///        pairs with `advance_epoch()`'s release store to give the caller a real
	///        happens-before edge onto [audio]'s prior-block work (see class
	///        comment). Used both to stamp newly-retired snapshots and to decide
	///        whether a stamped entry is now safe to free.
	[[nodiscard]] std::uint64_t audio_epoch() const noexcept { return epoch_.load(std::memory_order_acquire); }

private:
	std::atomic<std::uint64_t> epoch_{0};
};

/// @brief Default reclamation hook: plain `delete` through a pointer-to-const, which
///        is well-formed (a delete-expression accepts a pointer to a possibly
///        cv-qualified object type) -- so the default case needs no const_cast and
///        `Published<T>` never has to expose a mutable `T*` anywhere in its API.
template <typename T>
struct DefaultPublishedDeleter {
	void operator()(const T* p) const noexcept { delete p; }
};

/// @brief An atomically-swapped pointer to an immutable `T`: the "publish spine"
///        primitive. One [audio] reader thread wait-free `load()`s the latest
///        snapshot; one [task] writer thread `publish()`es new snapshots and
///        `reclaim()`s retired ones once they're provably unreachable from [audio].
///
/// `Published<T>` never allocates or frees a live `T` on the read/write hot path --
/// `load()` and `publish()` are pure atomic ops. It DOES own freeing retired `T`s,
/// via the injectable `Deleter` (default: `delete`), so a later pool allocator can
/// slot in without changing this class's shape. Construction of the pointed-to `T`
/// is always the caller's/pool's responsibility.
///
/// @tparam T The immutable snapshot type. Always accessed through `const T*`.
/// @tparam Deleter Reclamation hook, `void operator()(const T*) const`. Default frees
///         with `delete`.
template <typename T, typename Deleter = DefaultPublishedDeleter<T>>
class Published {
public:
	// [audio]'s load() must be wait-free, not merely lock-free-in-the-typical-case;
	// fail to compile rather than silently taking a lock on a target where
	// std::atomic<const T*> isn't always lock-free.
	static_assert(std::atomic<const T*>::is_always_lock_free);

	/// @brief Retire-list size at which `retire()` forces an inline `reclaim()` sweep.
	///
	/// This caps free-list growth *only while [audio] keeps advancing its epoch* -- each
	/// forced `reclaim()` can then free every entry that has fallen two block-
	/// boundaries behind. If [audio] is not progressing (never started, stalled, or
	/// crashed), no entry ever becomes epoch-eligible, so the forced sweep frees
	/// nothing and the list keeps growing past this threshold regardless. That is
	/// inherent to epoch-based reclamation -- a non-progressing reader means nothing
	/// it may still hold can ever be proven unreachable -- and is not a bound this
	/// threshold can fix.
	static constexpr std::size_t kDefaultRetireThreshold = 8;

	/// @brief Construct with an epoch source and an optional already-published initial
	///        pointer.
	///
	/// @param domain The shared epoch source for this audio domain (see
	///        `SnapshotDomain`). Must outlive this `Published<T>`.
	/// @param initial Initial published pointer (may be `nullptr`); ownership is
	///        NOT taken over by this constructor call in any special way -- it is
	///        simply the first value `cur_` holds, freed like any other retired
	///        pointer when superseded or at destruction.
	/// @param deleter Reclamation hook, copied/stored.
	/// @param retire_threshold Retire-list size that forces an inline `reclaim()`.
	explicit Published(SnapshotDomain& domain, const T* initial = nullptr, Deleter deleter = Deleter{},
	                   std::size_t retire_threshold = kDefaultRetireThreshold)
	    : domain_(domain), cur_(initial), deleter_(std::move(deleter)), retire_threshold_(retire_threshold) {}

	Published(const Published&) = delete;
	Published& operator=(const Published&) = delete;
	Published(Published&&) = delete;
	Published& operator=(Published&&) = delete;

	/// @brief [task] Teardown. NOT safe to run concurrently with the [audio]
	///        reader -- callers must ensure the audio executor has stopped (or
	///        never started) before a `Published<T>` is destroyed. Frees the
	///        current pointer and every still-retired one unconditionally (no
	///        epoch check: there is no reader left to protect against).
	~Published() {
		for (const RetiredEntry& e : retired_) {
			deleter_(e.ptr);
		}
		retired_.clear();
		if (const T* p = cur_.load(std::memory_order_relaxed)) {
			deleter_(p);
		}
	}

	/// @brief [audio] Wait-free read. The audio thread should call this once per
	///        tier-pointer per render block, cache the result for the whole block,
	///        and read only through that cached pointer -- never re-`load()`
	///        mid-block. `T` is immutable, so the returned pointer never tears and
	///        needs no lock.
	[[nodiscard]] const T* load() const noexcept { return cur_.load(std::memory_order_acquire); }

	/// @brief [task] Publish a new snapshot, retiring the old one. `next` must be a
	///        fully-constructed, immutable `T` the caller is handing off ownership
	///        of (typically pool- or new-allocated). May run `reclaim()`
	///        internally if the retire list has grown past `retire_threshold_` --
	///        i.e. this call may take time / touch the allocator; it is a [task]-
	///        only operation.
	void publish(const T* next) {
		// Debug-only guard: republishing the pointer that is already current would
		// immediately retire it while [audio] may still be reading it through the
		// prior load() -- a caller bug ("free of still-current"), not something this
		// class can make safe. Compiled out under NDEBUG; does not affect the
		// exchange's semantics or ordering below.
		assert(next != cur_.load(std::memory_order_relaxed) && "publish() called with the already-current pointer");
		// acq_rel: acquire so this exchange is ordered after any prior publish (not
		// load-bearing for correctness since [task] is single-writer, but keeps the
		// modification order intuitive); release so [audio]'s subsequent acquire
		// load() is guaranteed to see a fully-constructed `*next`.
		const T* old = cur_.exchange(next, std::memory_order_acq_rel);
		retire(old);
	}

	/// @brief [task] Sweep the retire list, freeing every entry whose stamped
	///        epoch is at least two block-boundaries behind the current audio
	///        epoch.
	///
	/// @par The two-grace-period safety argument
	/// When `retire(p)` runs, it stamps `p` with `E = domain_.audio_epoch()` -- the
	/// last audio-block boundary [task] can prove happened (via the acquire/release
	/// pairing documented on `SnapshotDomain`). At that moment, [audio] may still be
	/// mid-block, holding `p` from a `load()` earlier in block E (or, thanks to
	/// acquire/release, block E's `load()` might not even be visible to [task] yet --
	/// the stamp is a conservative lower bound, never a claim about what audio
	/// currently holds). [audio] never carries a pointer across a block boundary: it
	/// re-`load()`s current at the start of every block, so whatever it held during
	/// block E is unreachable from [audio] as of the *start* of block E+1, and its
	/// `advance_epoch()` for block E+1 is exactly the release-store that publishes
	/// that fact. By the time [task] observes `audio_epoch() >= E + 2` (an acquire
	/// load, synchronizing-with that release store or a later one), [audio] has
	/// completed block E+1's `advance_epoch()` -- meaning block E's `load()`-side
	/// reads of `p` are not merely "probably done," they *happen-before* this
	/// observation. Freeing `p` at that point is therefore race-free, not just
	/// temporally safe. The `+ 2` (rather than `+ 1`) is the standard grace-period
	/// margin: it covers the case where `retire()`'s stamp read raced ahead of
	/// [audio]'s own `advance_epoch()` for block E (i.e. `E` under-counts by up to
	/// one block), so one full extra boundary is required to be certain.
	void reclaim() noexcept {
		const std::uint64_t audio_epoch = domain_.audio_epoch();
		std::size_t keep = 0;
		for (std::size_t i = 0; i < retired_.size(); ++i) {
			const RetiredEntry& e = retired_[i];
			if (e.stamp_epoch + 2 <= audio_epoch) {
				deleter_(e.ptr);
			}
			else {
				retired_[keep++] = e;
			}
		}
		retired_.resize(keep);
	}

	/// @brief [task] Number of snapshots currently on the retire list, awaiting a
	///        grace period. Test/diagnostic hook.
	[[nodiscard]] std::size_t retired_count() const noexcept { return retired_.size(); }

private:
	struct RetiredEntry {
		const T* ptr;
		std::uint64_t stamp_epoch;
	};

	/// @brief [task] Move a superseded pointer onto the retire list, stamped with
	///        the current audio epoch, forcing an inline sweep if the list has
	///        grown past the bound.
	void retire(const T* p) {
		if (p == nullptr) {
			return;
		}
		retired_.push_back(RetiredEntry{p, domain_.audio_epoch()});
		if (retired_.size() > retire_threshold_) {
			reclaim();
		}
	}

	SnapshotDomain& domain_;
	std::atomic<const T*> cur_;
	Deleter deleter_;
	std::size_t retire_threshold_;
	std::vector<RetiredEntry> retired_; ///< [task]-owned only; [audio] never touches this.
};

} // namespace deluge::util
