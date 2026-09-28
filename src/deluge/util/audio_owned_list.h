#pragma once

// AudioOwnedList -- a list of pointers the audio render walks while another tier adds and removes entries.
//
// Every change and every walk runs inside an AudioExcludedSection (libdeluge/system.h), so a walk never sees the
// list mid-change and never sees an entry after remove() returned: on the device the render cannot be preempted
// mid-walk and every other context masks around its change; on host every thread takes the one lock. The container
// is private, so nothing can change it any other way.
//
// remove() must complete before the entry's memory is released; after it returns no walk can reach the entry.

#include "libdeluge/system.h"

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace deluge {

template <class T, class Container = std::vector<T*>>
class AudioOwnedList {
public:
	/// @brief Add @p entry to the list. As with remove(), no walk can observe a partial change.
	/// @param entry The entry to add.
	void add(T* entry) {
		AudioExcludedSection section;
		entries_.push_back(entry);
	}

	/// @brief Remove @p entry from the list. Once this returns, no walk can reach it.
	/// @param entry The entry to remove.
	void remove(T* entry) {
		AudioExcludedSection section;
		std::erase(entries_, entry);
	}

	/// @brief Remove every entry @p pred holds for, in one pass over the list. As with remove(), no walk reaches a
	///        removed entry once this returns.
	/// @param pred Predicate deciding which entries to remove.
	/// @return How many entries were removed.
	template <class Pred>
	std::size_t remove_if(Pred&& pred) {
		AudioExcludedSection section;
		return std::erase_if(entries_, std::forward<Pred>(pred));
	}

	/// @brief Call `f(const Container&)` with the list held still. Consume any view of it inside `f`.
	/// @param f Callable invoked with the held-still container.
	/// @return Whatever @p f returns.
	template <class F>
	decltype(auto) walk(F&& f) const {
		AudioExcludedSection section;
		return std::forward<F>(f)(std::as_const(entries_));
	}

private:
	Container entries_;
};

} // namespace deluge
