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
#include <new>

#ifndef GREATER_OR_EQUAL
#define GREATER_OR_EQUAL 0
#define LESS (-1)
#endif

namespace deluge {

/// Drop-in replacement for OrderedResizeableArrayWith32bitKey, backed by deluge::fast_vector.
/// T must have a public int32_t `pos` member, which is the (ascending) sort key.
///
/// The legacy method names and semantics are kept so the very large number of call sites stay unchanged;
/// they can be modernised incrementally later.
template <typename T>
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

	[[nodiscard]] int32_t getNumElements() const { return static_cast<int32_t>(elements_.size()); }
	T* getElementAddress(int32_t index) { return &elements_[index]; }
	T const* getElementAddress(int32_t index) const { return &elements_[index]; }
	[[nodiscard]] int32_t getKeyAtIndex(int32_t i) const { return elements_[i].pos; }
	void setKeyAtIndex(int32_t key, int32_t i) { elements_[i].pos = key; }

	iterator begin() { return elements_.begin(); }
	iterator end() { return elements_.end(); }
	const_iterator begin() const { return elements_.begin(); }
	const_iterator end() const { return elements_.end(); }

	/// With duplicate keys, returns the leftmost matching (or greater) one if doing GREATER_OR_EQUAL, or the
	/// rightmost lesser one if doing LESS.
	[[nodiscard]] int32_t search(int32_t searchKey, int32_t comparison, int32_t rangeBegin, int32_t rangeEnd) const {
		auto it = std::lower_bound(elements_.begin() + rangeBegin, elements_.begin() + rangeEnd, searchKey,
		                           [](T const& element, int32_t key) { return element.pos < key; });
		return static_cast<int32_t>(it - elements_.begin()) + comparison;
	}
	[[nodiscard]] int32_t search(int32_t searchKey, int32_t comparison, int32_t rangeBegin = 0) const {
		return search(searchKey, comparison, rangeBegin, getNumElements());
	}

	/// Returns -1 if not found
	[[nodiscard]] int32_t searchExact(int32_t key) const {
		int32_t i = search(key, GREATER_OR_EQUAL);
		if (i < getNumElements() && elements_[i].pos == key) {
			return i;
		}
		return -1;
	}

	/// Batch search: results, as if GREATER_OR_EQUAL had been supplied to search(), are written back into
	/// searchTerms. The terms must be in ascending order.
	void searchMultiple(int32_t* __restrict__ searchTerms, int32_t numSearchTerms, int32_t rangeEnd = -1) const {
		if (rangeEnd == -1) {
			rangeEnd = getNumElements();
		}
		int32_t rangeBegin = 0;
		for (int32_t t = 0; t < numSearchTerms; t++) {
			rangeBegin = search(searchTerms[t], GREATER_OR_EQUAL, rangeBegin, rangeEnd);
			searchTerms[t] = rangeBegin;
		}
	}

	/// Like searchMultiple(), for exactly 2 ascending search terms.
	void searchDual(int32_t const* __restrict__ searchTerms, int32_t* __restrict__ resultingIndexes) const {
		resultingIndexes[0] = search(searchTerms[0], GREATER_OR_EQUAL);
		resultingIndexes[1] = search(searchTerms[1], GREATER_OR_EQUAL, resultingIndexes[0]);
	}

	Error insertAtIndex(int32_t i, int32_t numToInsert = 1) {
		try {
			elements_.insert(elements_.begin() + i, numToInsert, T{});
		} catch (deluge::exception&) {
			return Error::INSUFFICIENT_RAM;
		}
		return Error::NONE;
	}

	void deleteAtIndex(int32_t i, int32_t numToDelete = 1, bool mayShortenMemoryAfter = true) {
		// mayShortenMemoryAfter is accepted for legacy-API compatibility; vector capacity is never shrunk by
		// erase, so the "don't shorten so the next insert can't fail" contract holds regardless.
		(void)mayShortenMemoryAfter;
		elements_.erase(elements_.begin() + i, elements_.begin() + i + numToDelete);
	}

	/// Returns index created, or -1 if error
	int32_t insertAtKey(int32_t key, bool isDefinitelyLast = false) {
		int32_t i = isDefinitelyLast ? getNumElements() : search(key, GREATER_OR_EQUAL);
		if (insertAtIndex(i) != Error::NONE) {
			return -1;
		}
		elements_[i].pos = key;
		return i;
	}

	void deleteAtKey(int32_t key) {
		int32_t i = searchExact(key);
		if (i != -1) {
			deleteAtIndex(i);
		}
	}

	void empty() { elements_.clear(); }

	void swapStateWith(OrderedPosVector* other) { elements_.swap(other->elements_); }

	void swapElements(int32_t i1, int32_t i2) { std::swap(elements_[i1], elements_[i2]); }

	bool ensureEnoughSpaceAllocated(int32_t numAdditionalElementsNeeded) {
		try {
			elements_.reserve(elements_.size() + numAdditionalElementsNeeded);
			return true;
		} catch (deluge::exception&) {
			return false;
		}
	}

	bool cloneFrom(OrderedPosVector const* other) {
		try {
			elements_ = other->elements_;
			return true;
		} catch (deluge::exception&) {
			return false;
		}
	}

	/// Call after this object was byte-copied from another instance (the legacy cloning idiom): gives this copy
	/// its own buffer. Until this is called, the byte-copy aliases the original's buffer, which the original
	/// still owns; on failure this copy is detached and left empty.
	Error beenCloned() {
		try {
			fast_vector<T> copy(elements_);                   // Deep copy; reading through the aliased buffer is safe
			new (&elements_) fast_vector<T>(std::move(copy)); // Overwrite the alias without destructing it
			return Error::NONE;
		} catch (deluge::exception&) {
			new (&elements_) fast_vector<T>();
			return Error::INSUFFICIENT_RAM;
		}
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

		int32_t cutoffIndex = search(cutoffPos, GREATER_OR_EQUAL);

		for (int32_t i = 0; i < cutoffIndex; i++) {
			elements_[i].pos += amount;
		}
		for (int32_t i = cutoffIndex; i < getNumElements(); i++) {
			elements_[i].pos += amount - effectiveLength;
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
		int32_t iEndPosWithinFirstRepeat = search(endPosWithinFirstRepeat, GREATER_OR_EQUAL);

		// Do this rather than just taking the size - this ensures we ignore / chop off any elements >= wrapPoint
		int32_t oldNum = search(wrapPoint, GREATER_OR_EQUAL);
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
				elements_[i + oldNum * r].pos = elements_[i].pos + wrapPoint * r;
			}
		}

		return true;
	}

	/// Test that the keys in this array are sorted in ascending order. Used in assert-like fashion.
	void testSequentiality(char const* errorCode) const {
#if ENABLE_SEQUENTIALITY_TESTS
		int32_t lastKey = -2147483648;
		for (int32_t i = 0; i < getNumElements(); i++) {
			int32_t key = elements_[i].pos;
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
