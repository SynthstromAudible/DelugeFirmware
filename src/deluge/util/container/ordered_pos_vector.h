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

#include "definitions_cxx.hpp"
#include "util/containers.h"
#include <algorithm>
#include <cstring>
#include <expected>
#include <new>
#include <type_traits>

namespace deluge {

/// A sorted vector backed by deluge::fast_vector; the (ascending) sort key is the member named by
/// keyMember - by default T::pos. Replaces the old OrderedResizeableArray(With32bitKey).
///
/// Beyond the standard surface, it offers: key-based lookups (lower_bound/find_key/firstAtOrAfter/tryGet),
/// fallible std::expected mutations (insertAt/insertSorted/reserveExtra), the sequencer batch algorithms
/// (searchMultiple/searchDual/shiftHorizontal/generateRepeats), and the legacy byte-copy clone idiom
/// (init/cloneFrom/beenCloned) which survives until real fallible clone factories replace it.
template <typename T, auto keyMember = &T::pos>
class OrderedPosVector {
public:
	OrderedPosVector() = default;
	~OrderedPosVector() = default;
	OrderedPosVector(OrderedPosVector&&) noexcept = default;
	OrderedPosVector& operator=(OrderedPosVector&&) noexcept = default;
	// No implicit copying: the legacy byte-copy cloning idiom and C++ copies must not be mixed up.
	// Use cloneFrom() for an explicit, fallible deep copy.
	OrderedPosVector(OrderedPosVector const&) = delete;
	OrderedPosVector& operator=(OrderedPosVector const&) = delete;

	/// Forget the current contents *without* freeing them. Only for use when this object is a byte-copy of
	/// another instance and so doesn't own what it points at (the legacy cloning idiom).
	void init() { new (&elements_) fast_vector<T>(); }

	using iterator = typename fast_vector<T>::iterator;
	using const_iterator = typename fast_vector<T>::const_iterator;

	/// Index of the first element whose key is >= the given key (size() if none). The index-returning
	/// counterpart of lower_bound(), for the many call sites that do index arithmetic around the result.
	[[nodiscard]] int32_t firstAtOrAfter(int32_t key, int32_t searchFrom = 0) const {
		auto it = std::ranges::lower_bound(elements_.begin() + searchFrom, elements_.end(), key, {}, keyMember);
		return static_cast<int32_t>(it - elements_.begin());
	}

	/// Inserts numToInsert default-constructed elements at index i.
	std::expected<void, Error> insertAt(int32_t i, int32_t numToInsert = 1) {
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
		} catch (deluge::exception&) {
			return std::unexpected(Error::INSUFFICIENT_RAM);
		}
		return {};
	}

	/// Inserts a default-constructed element with the given key at its sorted position; returns its index.
	std::expected<int32_t, Error> insertSorted(int32_t key, bool isDefinitelyLast = false) {
		int32_t i = isDefinitelyLast ? static_cast<int32_t>(elements_.size()) : firstAtOrAfter(key);
		auto inserted = insertAt(i);
		if (!inserted) {
			return std::unexpected(inserted.error());
		}
		elements_[i].*keyMember = key;
		return i;
	}

	/// Reserves room for this many elements beyond the current size.
	std::expected<void, Error> reserveExtra(int32_t numAdditionalElementsNeeded) {
		try {
			elements_.reserve(elements_.size() + numAdditionalElementsNeeded);
		} catch (deluge::exception&) {
			return std::unexpected(Error::INSUFFICIENT_RAM);
		}
		return {};
	}

	void swap(OrderedPosVector& other) noexcept { elements_.swap(other.elements_); }

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
		return (it != elements_.end() && (*it).*keyMember == key) ? it : elements_.end();
	}

	/// Erases by iterator (returns the iterator following the removed range, as std containers do)
	iterator erase(iterator first, iterator last) { return elements_.erase(first, last); }
	iterator erase(iterator at) { return elements_.erase(at); }

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
		if (iFrom < iTo) {
			std::rotate(elements_.begin() + iFrom, elements_.begin() + iFrom + 1, elements_.begin() + iTo + 1);
		}
		else {
			std::rotate(elements_.begin() + iTo, elements_.begin() + iFrom, elements_.begin() + iFrom + 1);
		}
	}

	void deleteAtKey(int32_t key) {
		auto it = find_key(key);
		if (it != elements_.end()) {
			elements_.erase(it);
		}
	}

	// Standard container surface
	[[nodiscard]] size_t size() const { return elements_.size(); }
	[[nodiscard]] bool empty() const { return elements_.empty(); }
	void clear() { elements_.clear(); }
	T& operator[](size_t i) { return elements_[i]; }
	T const& operator[](size_t i) const { return elements_[i]; }
	T& front() { return elements_.front(); }
	T& back() { return elements_.back(); }
	T* data() { return elements_.data(); }

	bool cloneFrom(OrderedPosVector const* other) {
		try {
			elements_ = other->elements_;
			return true;
		} catch (deluge::exception&) {
			return false;
		}
	}

	/// Call after this object was byte-copied from another instance (the legacy cloning idiom): gives this copy
	/// its own buffer, holding *byte-copies* of the elements. For trivially-copyable element types that is a
	/// complete deep copy; for element types that themselves own memory, it reproduces the legacy semantics
	/// where each element still aliases the original's allocations until its own beenCloned()-style fix-up runs.
	/// Until this is called, the byte-copy aliases the original's buffer, which the original still owns; on
	/// failure this copy is detached and left empty.
	Error beenCloned() {
		int32_t num = static_cast<int32_t>(elements_.size());
		T const* sourceData = elements_.data(); // Reading through the aliased buffer is safe
		fast_vector<T> fresh;
		try {
			fresh.reserve(num);
		} catch (deluge::exception&) {
			new (&elements_) fast_vector<T>();
			return Error::INSUFFICIENT_RAM;
		}
		for (int32_t i = 0; i < num; i++) {
			fresh.emplace_back();
			std::memcpy(static_cast<void*>(&fresh.back()), static_cast<void const*>(&sourceData[i]), sizeof(T));
		}
		new (&elements_) fast_vector<T>(std::move(fresh)); // Overwrite the alias without destructing it
		return Error::NONE;
	}

	/// Rotates all keys forward by `amount` (which may be negative) within a span of `effectiveLength`,
	/// keeping the array sorted by rotating the elements correspondingly.
	void shiftHorizontal(int32_t amount, int32_t effectiveLength) {
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

	/// Tiles the contents (all of which must lie before wrapPoint) out to endPos. Returns false on OOM.
	bool generateRepeats(int32_t wrapPoint, int32_t endPos) {
		if (elements_.empty()) {
			return true;
		}

		int32_t numCompleteRepeats = static_cast<uint32_t>(endPos) / static_cast<uint32_t>(wrapPoint);
		int32_t endPosWithinFirstRepeat = endPos - numCompleteRepeats * wrapPoint;
		int32_t iEndPosWithinFirstRepeat = firstAtOrAfter(endPosWithinFirstRepeat);

		// Do this rather than just taking the size - this ensures we ignore / chop off any elements >= wrapPoint
		int32_t oldNum = firstAtOrAfter(wrapPoint);
		int32_t newNum = oldNum * numCompleteRepeats + iEndPosWithinFirstRepeat;

		try {
			elements_.resize(newNum);
		} catch (deluge::exception&) {
			return false;
		}

		for (int32_t r = 1; r <= numCompleteRepeats; r++) { // For each repeat
			for (int32_t i = 0; i < oldNum; i++) {
				if (r == numCompleteRepeats && i >= iEndPosWithinFirstRepeat) {
					break;
				}
				elements_[i + oldNum * r] = elements_[i];
				elements_[i + oldNum * r].*keyMember = elements_[i].*keyMember + wrapPoint * r;
			}
		}

		return true;
	}

	/// Test that the keys in this array are sorted in ascending order. Used in assert-like fashion.
	void testSequentiality(char const* errorCode) const {
#if ENABLE_SEQUENTIALITY_TESTS
		int32_t lastKey = -2147483648;
		for (int32_t i = 0; i < static_cast<int32_t>(elements_.size()); i++) {
			int32_t key = elements_[i].*keyMember;
			if (key <= lastKey) {
				FREEZE_WITH_ERROR(errorCode);
			}
			lastKey = key;
		}
#endif
	}

protected:
	fast_vector<T> elements_;
};

} // namespace deluge
