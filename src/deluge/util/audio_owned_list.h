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
#include <utility>
#include <vector>

namespace deluge::util {

template <class T, class Container = std::vector<T*>>
class AudioOwnedList {
public:
	void add(T* entry) {
		AudioExcludedSection section;
		entries_.push_back(entry);
	}

	void remove(T* entry) {
		AudioExcludedSection section;
		std::erase(entries_, entry);
	}

	/// Call `f(const Container&)` with the list held still. Consume any view of it inside `f`.
	template <class F>
	decltype(auto) walk(F&& f) const {
		AudioExcludedSection section;
		return std::forward<F>(f)(std::as_const(entries_));
	}

private:
	Container entries_;
};

} // namespace deluge::util
