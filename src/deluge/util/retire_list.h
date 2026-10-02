#pragma once

// RetireList -- deferred destruction for objects the audio render may still be reading.
//
// retire() stamps an object with the render's current epoch (SnapshotDomain, advanced by the render once at the
// start of every block); reclaim() destroys every object whose stamp is at least two epochs old -- the same
// two-grace-period rule as Published<T>::reclaim (see published.h for the full memory-model argument), and for the
// same reason: the epoch read at retire() can under-count the render's progress by one block. If the render has
// never run (epoch 0), there is no reader to protect against and retire() destroys at once.
//
// Single-threaded on the [task] side: retire() and reclaim() are called from the interaction tier only.
// Include only from .cpp files: published.h declares namespace deluge::util, which shadows the global ::util
// inside namespace deluge.
//
// Re-entrancy: a destroy callback may call retire() (pushing onto the same list mid-sweep is safe -- see
// retire()'s comment) but must not call reclaim(). A nested reclaim() call is made a harmless no-op by a
// reentrancy flag, rather than left to corrupt the list, since the guard costs one bool check.

#include "deluge/util/published.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace deluge {

/// @brief Epoch-stamped deferred-destruction list: the [task]-side counterpart to `SnapshotDomain` for objects
///        that aren't published through a `Published<T>` slot but may still be reachable from an in-flight
///        [audio] render block at the moment they're superseded.
class RetireList {
public:
	/// @param domain The shared epoch source for the audio domain this list retires against (see
	///        `util::SnapshotDomain`). Must outlive this `RetireList`.
	explicit RetireList(const util::SnapshotDomain& domain) : domain_(domain) {}

	/// @brief [task] Hand over ownership of `object`, stamped with the current audio epoch. Destroyed
	///        immediately if the render has never run (epoch 0: no reader to protect against), otherwise held
	///        until `reclaim()` proves it unreachable. Safe to call from inside a `destroy` callback running
	///        under `reclaim()`: the sweep below always re-reads `retired_.size()` and re-indexes freshly on
	///        every iteration, so an append (even one that reallocates) during the sweep is compacted
	///        correctly. `destroy` must NOT call `reclaim()` itself -- see the file banner and `reclaim()`.
	///        @p tag is the caller's: it is handed back to `reclaim()`'s predicate, so a caller can hold an entry
	///        past its grace period on a condition of its own (see `reclaim(MayDestroy)`).
	void retire(void* object, void (*destroy)(void*), std::uint64_t tag = 0) {
		// Store-buffering fix, paired with the seq_cst fence in `SnapshotDomain::advance_epoch()` right after
		// its epoch store: without this, the SB litmus lets this epoch read return a stale (pre-advance) value
		// while [audio]'s advance_epoch() has already committed the epoch that would prove `object` unreachable
		// -- see the comment on `advance_epoch()` for the full argument. Must run before the epoch read below.
		std::atomic_thread_fence(std::memory_order_seq_cst);
		const std::uint64_t epoch = domain_.audio_epoch();
		if (epoch == 0) {
			destroy(object);
			return;
		}
		retired_.push_back({object, destroy, epoch, tag});
	}

	/// @brief [task] Destroy every entry whose stamped epoch is at least two block-boundaries behind the
	///        current audio epoch -- see the file banner and `Published<T>::reclaim` for why two, not one.
	///        NOT re-entrant: a `destroy` callback that calls back into `reclaim()` would resume this loop
	///        with a stale `keep`/`i` after the inner call already compacted and resized `retired_` out from
	///        under it (double destroys or lost entries). A nested call is a no-op instead, via `reclaiming_`.
	void reclaim() {
		reclaim([](std::uint64_t) { return true; });
	}

	/// @brief [task] As `reclaim()`, but an entry past its grace period is destroyed only if `mayDestroy(tag)` also
	///        holds, with the tag it was retired with; otherwise it stays for a later call. Same re-entrancy rule.
	template <class MayDestroy>
	void reclaim(MayDestroy&& mayDestroy) {
		if (reclaiming_) {
			return;
		}
		reclaiming_ = true;
		const std::uint64_t epoch = domain_.audio_epoch();
		std::size_t keep = 0;
		for (std::size_t i = 0; i < retired_.size(); ++i) {
			const Entry& e = retired_[i];
			if (e.stamp + 2 <= epoch && mayDestroy(e.tag)) {
				e.destroy(e.object);
			}
			else {
				retired_[keep++] = e;
			}
		}
		retired_.resize(keep);
		reclaiming_ = false;
	}

	/// @brief [task] Number of entries still awaiting their grace period. Test/diagnostic hook.
	[[nodiscard]] std::size_t size() const { return retired_.size(); }

private:
	struct Entry {
		void* object;
		void (*destroy)(void*);
		std::uint64_t stamp;
		std::uint64_t tag;
	};

	const util::SnapshotDomain& domain_;
	std::vector<Entry> retired_;
	bool reclaiming_ = false; ///< Guards against a `destroy` callback re-entering `reclaim()`.
};

} // namespace deluge
