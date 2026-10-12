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

#include "modulation/params/param_node_vector.h"

#include "memory/general_memory_allocator.h"
#include "modulation/params/param_node.h"
#include <cstdint>
#include <new>
#include <string.h>

ParamNodeVector::ParamNodeVector() : OrderedResizeableArrayWith32bitKey(sizeof(ParamNode)) {
}

ParamNode* ParamNodeVector::getElement(int32_t index) {
	if (index < 0 || index >= getNumElements()) {
		return nullptr;
	}
	return (ParamNode*)getElementAddress(index);
}

ParamNode* ParamNodeVector::getFirst() {
	return getElement(0);
}

ParamNode* ParamNodeVector::getLast() {
	return getElement(getNumElements() - 1);
}

bool LazyParamNodeVector::allocate() {
	if (vector_) {
		return true;
	}
	void* memory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(ParamNodeVector));
	if (!memory) {
		return false;
	}
	vector_ = new (memory) ParamNodeVector();
	return true;
}

void LazyParamNodeVector::empty() {
	if (vector_) {
		vector_->~ParamNodeVector();
		delugeDealloc(vector_);
		vector_ = nullptr;
	}
}

void LazyParamNodeVector::freeIfEmpty() {
	if (vector_ && !vector_->getNumElements()) {
		empty();
	}
}

bool LazyParamNodeVector::cloneFrom(LazyParamNodeVector const* other) {
	vector_ = nullptr;
	if (!other->getNumElements()) {
		return true;
	}
	if (!allocate()) {
		return false;
	}
	bool success = vector_->cloneFrom(other->vector_);
	freeIfEmpty();
	return success;
}

Error LazyParamNodeVector::beenCloned() {
	ParamNodeVector* source = vector_; // Still belongs to the object we were memcpy'd from
	vector_ = nullptr;
	if (!source || !source->getNumElements()) {
		return Error::NONE;
	}
	if (!allocate()) {
		return Error::INSUFFICIENT_RAM;
	}
	if (!vector_->cloneFrom(source)) {
		empty();
		return Error::INSUFFICIENT_RAM;
	}
	return Error::NONE;
}

void LazyParamNodeVector::searchDual(int32_t const* __restrict__ searchTerms, int32_t* __restrict__ resultingIndexes) {
	if (vector_) {
		vector_->searchDual(searchTerms, resultingIndexes);
	}
	else {
		resultingIndexes[0] = 0;
		resultingIndexes[1] = 0;
	}
}

Error LazyParamNodeVector::insertAtIndex(int32_t i, int32_t numToInsert, void* thingNotToStealFrom) {
	if (!allocate()) {
		return Error::INSUFFICIENT_RAM;
	}
	Error error = vector_->insertAtIndex(i, numToInsert, thingNotToStealFrom);
	freeIfEmpty();
	return error;
}

int32_t LazyParamNodeVector::insertAtKey(int32_t key, bool isDefinitelyLast) {
	if (!allocate()) {
		return -1;
	}
	int32_t i = vector_->insertAtKey(key, isDefinitelyLast);
	freeIfEmpty();
	return i;
}

bool LazyParamNodeVector::ensureEnoughSpaceAllocated(int32_t numAdditionalElementsNeeded) {
	if (!allocate()) {
		return false;
	}
	bool success = vector_->ensureEnoughSpaceAllocated(numAdditionalElementsNeeded);
	if (!success) {
		freeIfEmpty();
	}
	return success;
}

void LazyParamNodeVector::deleteAtIndex(int32_t i, int32_t numToDelete, bool mayShortenMemoryAfter) {
	if (!vector_) {
		return;
	}
	vector_->deleteAtIndex(i, numToDelete, mayShortenMemoryAfter);
	if (mayShortenMemoryAfter) {
		freeIfEmpty();
	}
}
