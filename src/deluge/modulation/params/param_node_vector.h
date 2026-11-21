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

#include "modulation/params/param_value.h"
#include "util/container/array/ordered_resizeable_array.h"
#include <cstring>
#include <utility>

class ParamNode;

class ParamNodeVector : public OrderedResizeableArrayWith32bitKey {
public:
	ParamNodeVector();
	// Playback state exists only while a curve is allocated.
	AutoParamValue value;
	// Interpolation to calculate the current value. Which of these is in use depends on
	// ParamCollection::shouldInterpolateWithFloat(), so anything reading the increment has to be told which
	// by the ParamCollection that owns the AutoParam.
	union {
		int64_t valueIncrementPerHalfTick{};
		float value_increment_per_half_tick_float;
	};
	float interpolation_increment_remainder_float_ = 0;
	// If 0, it's off. If 1, it's latched until we hit some nodes / automation. "Latching" happens when you
	// start recording values, but then stops if you arrive at any pre-existing values. So it only works in
	// empty stretches of time.
	uint32_t renewedOverridingAtTime = 0;

	void copyPlaybackStateFrom(const ParamNodeVector& other) {
		value = other.value;
		// The source's evaluation belongs to its own owner's baseline.
		value.invalidate();
		// Copy the selected interpolation representation without reading an inactive union member.
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
		std::memcpy(&valueIncrementPerHalfTick, &other.valueIncrementPerHalfTick, sizeof(valueIncrementPerHalfTick));
		interpolation_increment_remainder_float_ = other.interpolation_increment_remainder_float_;
		renewedOverridingAtTime = other.renewedOverridingAtTime;
	}
	ParamNode* getElement(int32_t index);
	ParamNode* getFirst();
	ParamNode* getLast();
};

static_assert(alignof(ParamNodeVector) >= 2);

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
	void init() { setVector(nullptr); }
	/// Free the vector and all its nodes.
	void empty();
	/// Like ResizeableArray::cloneFrom(), this assumes there's currently nothing here to free.
	bool cloneFrom(LazyParamNodeVector const* other);
	/// Call after this object was memcpy'd from another one, to give it its own copy of the nodes.
	Error beenCloned();
	void swapStateWith(LazyParamNodeVector* other) {
		auto* saved = get();
		setVector(other->get());
		other->setVector(saved);
	}

	/// The underlying vector, or nullptr if there are no nodes.
	// Preserve the pointer representation while removing its separately owned binding tag.
	[[nodiscard]] ParamNodeVector* get() const {
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast, performance-no-int-to-ptr)
		return reinterpret_cast<ParamNodeVector*>(vectorAndBinding_ & ~uintptr_t{1});
	}
	// The binding belongs to the AutoParam, not to the curve being swapped or deleted.
	[[nodiscard]] bool hasClipValue() const { return (vectorAndBinding_ & 1) != 0; }
	void setHasClipValue(bool bound) { vectorAndBinding_ = (vectorAndBinding_ & ~uintptr_t{1}) | uintptr_t{bound}; }

	[[nodiscard]] int32_t getNumElements() const { return (get() != nullptr) ? get()->getNumElements() : 0; }
	[[nodiscard]] ParamNode* getElement(int32_t index) const {
		return (get() != nullptr) ? get()->getElement(index) : nullptr;
	}
	[[nodiscard]] ParamNode* getFirst() const { return (get() != nullptr) ? get()->getFirst() : nullptr; }
	[[nodiscard]] ParamNode* getLast() const { return (get() != nullptr) ? get()->getLast() : nullptr; }
	[[nodiscard]] void* getElementAddress(int32_t index) const { return get()->getElementAddress(index); }

	int32_t search(int32_t key, int32_t comparison, int32_t rangeBegin = 0) {
		return (get() != nullptr) ? get()->search(key, comparison, rangeBegin) : rangeBegin + comparison;
	}
	int32_t search(int32_t key, int32_t comparison, int32_t rangeBegin, int32_t rangeEnd) {
		return (get() != nullptr) ? get()->search(key, comparison, rangeBegin, rangeEnd) : rangeBegin + comparison;
	}
	[[nodiscard]] int32_t searchExact(int32_t key) const { return (get() != nullptr) ? get()->searchExact(key) : -1; }
	void searchDual(int32_t const* __restrict__ searchTerms, int32_t* __restrict__ resultingIndexes);
	void testSequentiality(char const* errorCode) {
		if (get() != nullptr) {
			get()->testSequentiality(errorCode);
		}
	}

	Error insertAtIndex(int32_t i, int32_t numToInsert = 1, void* thingNotToStealFrom = nullptr);
	int32_t insertAtKey(int32_t key, bool isDefinitelyLast = false);
	bool ensureEnoughSpaceAllocated(int32_t numAdditionalElementsNeeded);
	void deleteAtIndex(int32_t i, int32_t numToDelete = 1, bool mayShortenMemoryAfter = true);

	void shiftHorizontal(int32_t amount, int32_t effectiveLength) {
		if (get() != nullptr) {
			get()->shiftHorizontal(amount, effectiveLength);
		}
	}
	bool generateRepeats(int32_t wrapPoint, int32_t endPos) {
		return (get() != nullptr) ? get()->generateRepeats(wrapPoint, endPos) : true;
	}

private:
	bool allocate();
	void freeIfEmpty();

	void setVector(ParamNodeVector* vector) {
		// A curve from another owner was evaluated against that owner's baseline.
		if (vector != nullptr) {
			vector->value.invalidate();
		}
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		vectorAndBinding_ = reinterpret_cast<uintptr_t>(vector) | (vectorAndBinding_ & 1);
	}
	// Structural edits can leave an allocated curve empty, which reads as unautomated.
	void invalidateValue() const {
		if (get() != nullptr) {
			get()->value.invalidate();
		}
	}
	uintptr_t vectorAndBinding_ = 0;
};
