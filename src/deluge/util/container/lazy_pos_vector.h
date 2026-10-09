/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "memory/fast_allocator.h"
#include "util/container/ordered_pos_vector.h"
#include <utility>

namespace deluge {

/// @brief An OrderedPosVector that costs one pointer until something is put in it.
///
/// For containers that are almost always empty and exist in large numbers: every AutoParam has a node list, and only
/// automated ones have nodes. The vector itself lives on the heap, made by the first insert, so an empty one is a null
/// pointer. It is freed by clear() and by destruction, never merely for becoming empty: the render's own automation
/// recording erases nodes, and freeing there would put a heap free on the render tier for no saving worth having.
///
/// Same interface as OrderedPosVector, and every read of an unmade vector behaves as on an empty one. Making the vector
/// is a structural edit like any other: built outside an AudioExcludedSection, published inside one.
template <typename T, auto keyMember = &T::pos>
class LazyPosVector {
	using Impl = OrderedPosVector<T, keyMember>;

public:
	using iterator = typename Impl::iterator;
	using const_iterator = typename Impl::const_iterator;

	LazyPosVector() = default;
	~LazyPosVector() { destroy(impl_); }
	LazyPosVector(LazyPosVector&& other) noexcept : impl_{std::exchange(other.impl_, nullptr)} {}
	LazyPosVector& operator=(LazyPosVector&& other) noexcept {
		Impl* old = nullptr;
		{
			AudioExcludedSection section; // the render walks this container: see Impl::noteEdit()
			noteEdit();
			old = std::exchange(impl_, std::exchange(other.impl_, nullptr));
		}
		destroy(old);
		return *this;
	}
	// No implicit copying, as for OrderedPosVector: use cloneFrom().
	LazyPosVector(LazyPosVector const&) = delete;
	LazyPosVector& operator=(LazyPosVector const&) = delete;

	/// Forget the contents *without* freeing them: this object is a byte-copy of another, which still owns them.
	void init() {
		AudioExcludedSection section; // the render walks this container: see Impl::noteEdit()
		noteEdit();
		impl_ = nullptr;
	}

	[[nodiscard]] int32_t firstAtOrAfter(int32_t key, int32_t searchFrom = 0) const {
		return impl_ ? impl_->firstAtOrAfter(key, searchFrom) : searchFrom;
	}

	std::expected<void, Error> insertAt(int32_t i, int32_t numToInsert = 1) {
		if (!make()) {
			return std::unexpected(Error::INSUFFICIENT_RAM);
		}
		return impl_->insertAt(i, numToInsert);
	}

	std::expected<int32_t, Error> insertSorted(int32_t key, bool isDefinitelyLast = false) {
		if (!make()) {
			return std::unexpected(Error::INSUFFICIENT_RAM);
		}
		return impl_->insertSorted(key, isDefinitelyLast);
	}

	std::expected<void, Error> reserveExtra(int32_t numAdditionalElementsNeeded) {
		if (!make()) {
			return std::unexpected(Error::INSUFFICIENT_RAM);
		}
		return impl_->reserveExtra(numAdditionalElementsNeeded);
	}

	void swap(LazyPosVector& other) noexcept {
		AudioExcludedSection section; // the render walks this container: see Impl::noteEdit()
		noteEdit();
		std::swap(impl_, other.impl_);
	}

	iterator begin() { return impl_ ? impl_->begin() : iterator{}; }
	iterator end() { return impl_ ? impl_->end() : iterator{}; }
	const_iterator begin() const { return impl_ ? std::as_const(*impl_).begin() : const_iterator{}; }
	const_iterator end() const { return impl_ ? std::as_const(*impl_).end() : const_iterator{}; }

	[[nodiscard]] T* tryGet(int32_t index) { return impl_ ? impl_->tryGet(index) : nullptr; }
	[[nodiscard]] T* tryGetLast() { return impl_ ? impl_->tryGetLast() : nullptr; }
	[[nodiscard]] iterator lower_bound(int32_t key) { return impl_ ? impl_->lower_bound(key) : iterator{}; }
	[[nodiscard]] const_iterator lower_bound(int32_t key) const {
		return impl_ ? std::as_const(*impl_).lower_bound(key) : const_iterator{};
	}
	[[nodiscard]] iterator find_key(int32_t key) { return impl_ ? impl_->find_key(key) : iterator{}; }

	iterator erase(iterator first, iterator last) { return impl_ ? impl_->erase(first, last) : iterator{}; }
	iterator erase(iterator at) { return impl_ ? impl_->erase(at) : iterator{}; }

	void searchMultiple(int32_t* __restrict__ searchTerms, int32_t numSearchTerms, int32_t rangeEnd = -1) const {
		if (impl_) {
			impl_->searchMultiple(searchTerms, numSearchTerms, rangeEnd);
			return;
		}
		for (int32_t t = 0; t < numSearchTerms; t++) {
			searchTerms[t] = 0;
		}
	}
	void searchDual(int32_t const* __restrict__ searchTerms, int32_t* __restrict__ resultingIndexes) const {
		if (impl_) {
			impl_->searchDual(searchTerms, resultingIndexes);
			return;
		}
		resultingIndexes[0] = 0;
		resultingIndexes[1] = 0;
	}

	void repositionElement(int32_t iFrom, int32_t iTo) {
		if (impl_) {
			impl_->repositionElement(iFrom, iTo);
		}
	}
	void deleteAtKey(int32_t key) {
		if (impl_) {
			impl_->deleteAtKey(key);
		}
	}

	[[nodiscard]] size_t size() const { return impl_ ? impl_->size() : 0; }
	[[nodiscard]] bool empty() const { return impl_ == nullptr || impl_->empty(); }
	/// @brief Empty it and free the vector itself. The vector is let go inside the section and destroyed outside it.
	void clear() {
		Impl* old = nullptr;
		{
			AudioExcludedSection section; // the render walks this container: see Impl::noteEdit()
			noteEdit();
			old = std::exchange(impl_, nullptr);
		}
		destroy(old);
	}
	T& operator[](size_t i) { return (*impl_)[i]; }
	T const& operator[](size_t i) const { return (*impl_)[i]; }
	T& front() { return impl_->front(); }
	T& back() { return impl_->back(); }
	T* data() { return impl_ ? impl_->data() : nullptr; }

	/// @brief Make this a copy of @p other. Copying an empty one frees this one's vector.
	/// @return false, leaving this unchanged, if there wasn't the memory.
	bool cloneFrom(LazyPosVector const* other) {
		if (other->empty()) {
			clear();
			return true;
		}
		return make() && impl_->cloneFrom(other->impl_);
	}

	/// Call after this object was byte-copied from another (the legacy cloning idiom): gives this copy its own vector,
	/// holding byte-copies of the elements, as OrderedPosVector::beenCloned() does. Until then it shares the
	/// original's, which the original still owns; on failure it is detached and left empty.
	Error beenCloned() {
		Impl* const source = impl_;
		if (source == nullptr) {
			return Error::NONE;
		}
		Impl* own = build();
		Error error = Error::INSUFFICIENT_RAM;
		if (own != nullptr) {
			// The byte-copies are made into a vector nothing else can see yet, so the section in Impl::beenCloned() is
			// a formality there.
			std::memcpy(static_cast<void*>(own), static_cast<void const*>(source), sizeof(Impl));
			error = own->beenCloned();
			if (error != Error::NONE) {
				destroy(own);
				own = nullptr;
			}
		}
		AudioExcludedSection section; // the render walks this container: see Impl::noteEdit()
		noteEdit();
		impl_ = own;
		return error;
	}

	void shiftHorizontal(int32_t amount, int32_t effectiveLength) {
		if (impl_) {
			impl_->shiftHorizontal(amount, effectiveLength);
		}
	}
	bool generateRepeats(int32_t wrapPoint, int32_t endPos) {
		return impl_ ? impl_->generateRepeats(wrapPoint, endPos) : true;
	}

	void testSequentiality(char const* errorCode) const {
		if (impl_) {
			impl_->testSequentiality(errorCode);
		}
	}

private:
	static void noteEdit() { noteStructuralEdit(StructuralContainer::ORDERED_POS_VECTOR); }

	/// A new, empty vector on the heap, or nullptr if there's no memory.
	static Impl* build() {
		try {
			return new (memory::fast_allocator<Impl>{}.allocate(1)) Impl();
		} catch (deluge::exception&) {
			return nullptr;
		}
	}

	static void destroy(Impl* impl) {
		if (impl != nullptr) {
			impl->~Impl();
			memory::fast_allocator<Impl>{}.deallocate(impl, 1);
		}
	}

	/// Make the vector if there isn't one yet. @return Whether there is one now.
	bool make() {
		if (impl_ != nullptr) {
			return true;
		}
		Impl* fresh = build();
		if (fresh == nullptr) {
			return false;
		}
		AudioExcludedSection section; // the render walks this container: see Impl::noteEdit()
		noteEdit();
		impl_ = fresh;
		return true;
	}

	Impl* impl_{};
};

} // namespace deluge
