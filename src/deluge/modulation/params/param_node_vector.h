/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "util/container/array/ordered_resizeable_array.h"
#include <utility>

class ParamNode;

class ParamNodeVector : public OrderedResizeableArrayWith32bitKey {
public:
	ParamNodeVector();

	ParamNode* getElement(int32_t index);
	ParamNode* getFirst();
	ParamNode* getLast();
};

/// Owns a ParamNodeVector that is only allocated once the first node is added, and freed again once the last node is
/// removed. An un-automated AutoParam therefore costs one pointer instead of a whole (empty) ParamNodeVector, which
/// matters because every Sound has well over a hundred AutoParams per Clip.
///
/// The interface mirrors the subset of ParamNodeVector that AutoParam uses, including its "memcpy then init() or
/// beenCloned()" cloning convention, so read-only calls on an unallocated vector behave exactly as on an empty one.
class LazyParamNodeVector {
public:
	LazyParamNodeVector() = default;
	~LazyParamNodeVector() { empty(); }
	LazyParamNodeVector(LazyParamNodeVector const&) = delete;
	LazyParamNodeVector& operator=(LazyParamNodeVector const&) = delete;

	/// Forget the current vector without freeing it - for when this object was memcpy'd from another one.
	void init() { vector_ = nullptr; }
	/// Free the vector and all its nodes.
	void empty();
	/// Like ResizeableArray::cloneFrom(), this assumes there's currently nothing here to free.
	bool cloneFrom(LazyParamNodeVector const* other);
	/// Call after this object was memcpy'd from another one, to give it its own copy of the nodes.
	Error beenCloned();
	void swapStateWith(LazyParamNodeVector* other) { std::swap(vector_, other->vector_); }

	/// The underlying vector, or nullptr if there are no nodes.
	ParamNodeVector* get() { return vector_; }

	int32_t getNumElements() const { return vector_ ? vector_->getNumElements() : 0; }
	ParamNode* getElement(int32_t index) { return vector_ ? vector_->getElement(index) : nullptr; }
	ParamNode* getFirst() { return vector_ ? vector_->getFirst() : nullptr; }
	ParamNode* getLast() { return vector_ ? vector_->getLast() : nullptr; }
	void* getElementAddress(int32_t index) { return vector_->getElementAddress(index); }

	int32_t search(int32_t key, int32_t comparison, int32_t rangeBegin = 0) {
		return vector_ ? vector_->search(key, comparison, rangeBegin) : rangeBegin + comparison;
	}
	int32_t search(int32_t key, int32_t comparison, int32_t rangeBegin, int32_t rangeEnd) {
		return vector_ ? vector_->search(key, comparison, rangeBegin, rangeEnd) : rangeBegin + comparison;
	}
	int32_t searchExact(int32_t key) { return vector_ ? vector_->searchExact(key) : -1; }
	void searchDual(int32_t const* __restrict__ searchTerms, int32_t* __restrict__ resultingIndexes);
	void testSequentiality(char const* errorCode) {
		if (vector_) {
			vector_->testSequentiality(errorCode);
		}
	}

	Error insertAtIndex(int32_t i, int32_t numToInsert = 1, void* thingNotToStealFrom = nullptr);
	int32_t insertAtKey(int32_t key, bool isDefinitelyLast = false);
	bool ensureEnoughSpaceAllocated(int32_t numAdditionalElementsNeeded);
	void deleteAtIndex(int32_t i, int32_t numToDelete = 1, bool mayShortenMemoryAfter = true);

	void shiftHorizontal(int32_t amount, int32_t effectiveLength) {
		if (vector_) {
			vector_->shiftHorizontal(amount, effectiveLength);
		}
	}
	bool generateRepeats(int32_t wrapPoint, int32_t endPos) {
		return vector_ ? vector_->generateRepeats(wrapPoint, endPos) : true;
	}

private:
	bool allocate();
	void freeIfEmpty();

	ParamNodeVector* vector_ = nullptr;
};
