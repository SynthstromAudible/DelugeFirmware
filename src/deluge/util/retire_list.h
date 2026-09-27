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

#include "deluge/util/published.h"

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
	///        until `reclaim()` proves it unreachable.
	void retire(void* object, void (*destroy)(void*)) {
		const std::uint64_t epoch = domain_.audio_epoch();
		if (epoch == 0) {
			destroy(object);
			return;
		}
		retired_.push_back({object, destroy, epoch});
	}

	/// @brief [task] Destroy every entry whose stamped epoch is at least two block-boundaries behind the
	///        current audio epoch -- see the file banner and `Published<T>::reclaim` for why two, not one.
	void reclaim() {
		const std::uint64_t epoch = domain_.audio_epoch();
		std::size_t keep = 0;
		for (std::size_t i = 0; i < retired_.size(); ++i) {
			const Entry& e = retired_[i];
			if (e.stamp + 2 <= epoch) {
				e.destroy(e.object);
			}
			else {
				retired_[keep++] = e;
			}
		}
		retired_.resize(keep);
	}

	/// @brief [task] Number of entries still awaiting their grace period. Test/diagnostic hook.
	[[nodiscard]] std::size_t size() const { return retired_.size(); }

private:
	struct Entry {
		void* object;
		void (*destroy)(void*);
		std::uint64_t stamp;
	};

	const util::SnapshotDomain& domain_;
	std::vector<Entry> retired_;
};

} // namespace deluge
