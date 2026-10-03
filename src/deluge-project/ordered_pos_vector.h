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

#include "deluge-project/containers.h"
#include "deluge-project/edit_guard.h"
#include "foundation/panic.h"
#include <algorithm>
#include <cstring>
#include <expected>
#include <functional>
#include <new>
#include <span>
#include <type_traits>

namespace deluge::project {

/// A sorted vector on the host's model memory (ModelAllocator); the (ascending) sort key is projected by `keyMember` -
/// by default the data member T::pos. Replaces the old OrderedResizeableArray(With32bitKey).
///
/// `keyMember` may be a pointer-to-DATA-member (the usual case, e.g. `&T::pos`, which is also assignable so the
/// key-*writing* batch ops work) OR a pointer-to-const-GETTER (e.g. `&NoteRow::y`, when the key lives behind an
/// accessor — the model owns it). Getter keys are read-only: the key-writing ops (insertSorted/shiftHorizontal/
/// generateRepeats/insertAt-then-assign) static_assert a data-member key, so a getter-keyed instantiation that
/// never calls them (like NoteRowVector, which hand-rolls its inserts) compiles fine.
///
/// Beyond the standard surface, it offers: key-based lookups (lower_bound/find_key/firstAtOrAfter/tryGet),
/// fallible std::expected mutations (insertAt/insertSorted/reserveExtra), the sequencer batch algorithms
/// (searchMultiple/searchDual/shiftHorizontal/generateRepeats), and value semantics (copy/move ctor+assign) —
/// deep copies go through copy construction/assignment, which throw the host's out-of-memory exception
/// (model_out_of_memory()).
///
/// Every structural edit (insert, erase, reorder, reallocate, hand-over) runs under an EditGuard: the render walks
/// these vectors (a clip's note rows, a row's notes, automation nodes, an output's clip instances).
template <typename T, auto keyMember = &T::pos>
requires std::is_member_pointer_v<decltype(keyMember)>
class OrderedPosVector {
	static constexpr bool keyIsWritable = std::is_member_object_pointer_v<decltype(keyMember)>;

public:
	OrderedPosVector() = default;
	~OrderedPosVector() = default;
	OrderedPosVector(OrderedPosVector&&) noexcept = default;
	/// @brief A structural edit when this vector is one the render walks (a note-row drag swaps two rows' vectors).
	OrderedPosVector& operator=(OrderedPosVector&& other) noexcept {
		EditGuard guard;
		elements_ = std::move(other.elements_);
		return *this;
	}
	// Deep copy (element-wise, via the backing vector). May throw the host's out-of-memory exception, like the
	// container's other allocating ops; the fallible clone factories that call it catch and convert to
	// std::expected.
	OrderedPosVector(OrderedPosVector const& other) : elements_(other.elements_) {}
	/// @brief The copy is made outside the section, and the old elements destroyed outside it: only the handover is
	/// masked.
	OrderedPosVector& operator=(OrderedPosVector const& other) {
		model_vector<T> copy = other.elements_;
		{
			EditGuard guard;
			elements_.swap(copy);
		}
		return *this;
	}

	/// Forget the current contents *without* freeing them. Only for use when this object is a byte-copy of
	/// another instance and so doesn't own what it points at (the legacy cloning idiom).
	void init() {
		EditGuard guard;
		new (&elements_) model_vector<T>();
	}

	using iterator = typename model_vector<T>::iterator;
	using const_iterator = typename model_vector<T>::const_iterator;

	/// Index of the first element whose key is >= the given key (size() if none). The index-returning
	/// counterpart of lower_bound(), for the many call sites that do index arithmetic around the result.
	[[nodiscard]] int32_t firstAtOrAfter(int32_t key, int32_t searchFrom = 0) const {
		auto it = std::ranges::lower_bound(elements_.begin() + searchFrom, elements_.end(), key, {}, keyMember);
		return static_cast<int32_t>(it - elements_.begin());
	}

	/// Inserts numToInsert default-constructed elements at index i.
	std::expected<void, AllocError> insertAt(int32_t i, int32_t numToInsert = 1) {
		EditGuard guard;
		try {
			if constexpr (std::is_copy_constructible_v<T>) {
				elements_.insert(elements_.begin() + i, numToInsert, T{});
			}
			else {
				// Move-only T: reserve up front (the only step that can throw), then emplace one by one
				elements_.reserve(elements_.size() + numToInsert);
				for (int32_t n = 0; n < numToInsert; n++) {
					elements_.emplace(elements_.begin() + i);
				}
			}
		} catch (...) {
			return std::unexpected(AllocError::OUT_OF_MEMORY);
		}
		return {};
	}

	/// Raw resize — caller maintains the sort invariant, like insertAt.
	void resize(size_t newSize) {
		EditGuard guard;
		elements_.resize(newSize);
	}

	/// Inserts a default-constructed element with the given key at its sorted position; returns its index.
	std::expected<int32_t, AllocError> insertSorted(int32_t key, bool isDefinitelyLast = false) {
		static_assert(keyIsWritable, "insertSorted writes the key; needs a data-member key, not a getter");
		int32_t i = isDefinitelyLast ? static_cast<int32_t>(elements_.size()) : firstAtOrAfter(key);
		auto inserted = insertAt(i);
		if (!inserted) {
			return std::unexpected(inserted.error());
		}
		elements_[i].*keyMember = key;
		return i;
	}

	/// Reserves room for this many elements beyond the current size.
	std::expected<void, AllocError> reserveExtra(int32_t numAdditionalElementsNeeded) {
		EditGuard guard;
		try {
			elements_.reserve(elements_.size() + numAdditionalElementsNeeded);
		} catch (...) {
			return std::unexpected(AllocError::OUT_OF_MEMORY);
		}
		return {};
	}

	void swap(OrderedPosVector& other) noexcept {
		EditGuard guard;
		elements_.swap(other.elements_);
	}

	iterator begin() { return elements_.begin(); }
	iterator end() { return elements_.end(); }
	const_iterator begin() const { return elements_.begin(); }
	const_iterator end() const { return elements_.end(); }

	/// The element at this index, or nullptr if the index is out of range (including the -1 that "none"
	/// search results produce).
	[[nodiscard]] T* tryGet(int32_t index) {
		if (index < 0 || index >= static_cast<int32_t>(elements_.size())) {
			return nullptr;
		}
		return &elements_[index];
	}

	/// The last element, or nullptr if empty
	[[nodiscard]] T* tryGetLast() { return elements_.empty() ? nullptr : &elements_.back(); }

	/// First element whose key is >= the given key
	[[nodiscard]] iterator lower_bound(int32_t key) { return std::ranges::lower_bound(elements_, key, {}, keyMember); }
	[[nodiscard]] const_iterator lower_bound(int32_t key) const {
		return std::ranges::lower_bound(elements_, key, {}, keyMember);
	}

	/// Iterator to the element with exactly this key, or end()
	[[nodiscard]] iterator find_key(int32_t key) {
		auto it = lower_bound(key);
		return (it != elements_.end() && std::invoke(keyMember, *it) == key) ? it : elements_.end();
	}

	/// Erases by iterator (returns the iterator following the removed range, as std containers do)
	iterator erase(iterator first, iterator last) {
		EditGuard guard;
		return elements_.erase(first, last);
	}
	iterator erase(iterator at) {
		EditGuard guard;
		return elements_.erase(at);
	}

	/// Batch lower-bound search: each term is replaced with the index of the first element whose key is >= it.
	/// The terms must be in ascending order.
	void searchMultiple(int32_t* __restrict__ searchTerms, int32_t numSearchTerms, int32_t rangeEnd = -1) const {
		auto last = (rangeEnd == -1) ? elements_.end() : elements_.begin() + rangeEnd;
		auto from = elements_.begin();
		for (int32_t t = 0; t < numSearchTerms; t++) {
			from = std::ranges::lower_bound(from, last, searchTerms[t], {}, keyMember);
			searchTerms[t] = static_cast<int32_t>(from - elements_.begin());
		}
	}

	/// Like searchMultiple(), for exactly 2 ascending search terms.
	void searchDual(int32_t const* __restrict__ searchTerms, int32_t* __restrict__ resultingIndexes) const {
		resultingIndexes[0] = firstAtOrAfter(searchTerms[0]);
		resultingIndexes[1] = firstAtOrAfter(searchTerms[1], resultingIndexes[0]);
	}

	/// Moves the element at iFrom so it sits at iTo, shuffling those in between along by one.
	void repositionElement(int32_t iFrom, int32_t iTo) {
		EditGuard guard;
		if (iFrom < iTo) {
			std::rotate(elements_.begin() + iFrom, elements_.begin() + iFrom + 1, elements_.begin() + iTo + 1);
		}
		else {
			std::rotate(elements_.begin() + iTo, elements_.begin() + iFrom, elements_.begin() + iFrom + 1);
		}
	}

	void deleteAtKey(int32_t key) {
		EditGuard guard;
		auto it = find_key(key);
		if (it != elements_.end()) {
			elements_.erase(it);
		}
	}

	// Standard container surface
	[[nodiscard]] size_t size() const { return elements_.size(); }
	[[nodiscard]] bool empty() const { return elements_.empty(); }
	/// @brief The elements are destroyed outside the section: only the buffer handover is masked.
	void clear() {
		model_vector<T> old;
		{
			EditGuard guard;
			old.swap(elements_);
		}
	}
	T& operator[](size_t i) { return elements_[i]; }
	T const& operator[](size_t i) const { return elements_[i]; }
	T& front() { return elements_.front(); }
	T& back() { return elements_.back(); }
	T* data() { return elements_.data(); }

	/// Rotates all keys forward by `amount` (which may be negative) within a span of `effectiveLength`,
	/// keeping the array sorted by rotating the elements correspondingly.
	void shiftHorizontal(int32_t amount, int32_t effectiveLength) {
		static_assert(keyIsWritable, "shiftHorizontal writes keys; needs a data-member key, not a getter");
		EditGuard guard;
		if (elements_.empty()) {
			return;
		}

		// Wrap the amount to the length.
		if (amount >= effectiveLength) {
			amount = static_cast<uint32_t>(amount) % static_cast<uint32_t>(effectiveLength);
		}
		else if (amount <= -effectiveLength) {
			amount = -(static_cast<uint32_t>(-amount) % static_cast<uint32_t>(effectiveLength));
		}
		if (amount == 0) {
			return;
		}

		int32_t cutoffPos = -amount;
		if (cutoffPos < 0) {
			cutoffPos += effectiveLength;
		}
		if (amount < 0) {
			amount += effectiveLength;
		}

		int32_t cutoffIndex = firstAtOrAfter(cutoffPos);

		for (int32_t i = 0; i < cutoffIndex; i++) {
			elements_[i].*keyMember += amount;
		}
		for (int32_t i = cutoffIndex; i < static_cast<int32_t>(elements_.size()); i++) {
			elements_[i].*keyMember += amount - effectiveLength;
		}

		std::rotate(elements_.begin(), elements_.begin() + cutoffIndex, elements_.end());
	}

	/// @brief Tiles the contents (all of which must lie before wrapPoint) out to endPos. Returns false on OOM.
	/// The tiled contents are built in a new buffer outside the section; only the handover is masked.
	bool generateRepeats(int32_t wrapPoint, int32_t endPos) {
		static_assert(keyIsWritable, "generateRepeats writes keys; needs a data-member key, not a getter");
		if (elements_.empty()) {
			return true;
		}

		int32_t numCompleteRepeats = static_cast<uint32_t>(endPos) / static_cast<uint32_t>(wrapPoint);
		int32_t endPosWithinFirstRepeat = endPos - numCompleteRepeats * wrapPoint;
		int32_t iEndPosWithinFirstRepeat = firstAtOrAfter(endPosWithinFirstRepeat);

		// Do this rather than just taking the size - this ensures we ignore / chop off any elements >= wrapPoint
		int32_t oldNum = firstAtOrAfter(wrapPoint);
		int32_t newNum = oldNum * numCompleteRepeats + iEndPosWithinFirstRepeat;

		model_vector<T> tiled;
		try {
			tiled = elements_;
			tiled.resize(newNum);
		} catch (...) {
			return false;
		}

		for (int32_t r = 1; r <= numCompleteRepeats; r++) { // For each repeat
			for (int32_t i = 0; i < oldNum; i++) {
				if (r == numCompleteRepeats && i >= iEndPosWithinFirstRepeat) {
					break;
				}
				tiled[i + oldNum * r] = tiled[i];
				tiled[i + oldNum * r].*keyMember = tiled[i].*keyMember + wrapPoint * r;
			}
		}

		{
			EditGuard guard;
			elements_.swap(tiled);
		}
		return true;
	}

	/// Test that the keys in this array are sorted in ascending order. Used in assert-like fashion.
	void testSequentiality(char const* errorCode) const {
#if !defined(NDEBUG)
		int32_t lastKey = -2147483648;
		for (int32_t i = 0; i < static_cast<int32_t>(elements_.size()); i++) {
			int32_t key = std::invoke(keyMember, elements_[i]);
			if (key <= lastKey) {
				FREEZE_WITH_ERROR(errorCode);
			}
			lastKey = key;
		}
#endif
	}

protected:
	model_vector<T> elements_{};
};

} // namespace deluge::project
