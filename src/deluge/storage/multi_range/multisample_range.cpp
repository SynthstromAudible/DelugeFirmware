/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "storage/multi_range/multisample_range.h"
#include "memory/general_memory_allocator.h"
#include "util/functions.h"
#include <cstddef>
#include <new>

// Layout assertions. MultisampleRange inherits from MultiRange and both have data members, so it is
// not standard-layout by the C++ standard — but GCC computes the offsets correctly on ARM32.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
// sampleHolder must stay at its original offset (sizeof(MultiRange) on ARM32 = 8).
static_assert(offsetof(MultisampleRange, sampleHolder) == sizeof(MultiRange),
              "sampleHolder offset changed — check MultiRange base size or field ordering");
static_assert(offsetof(MultisampleRange, sampleHolder) % 8 == 0, "sampleHolder must be 8-byte aligned");
static_assert(offsetof(MultisampleRange, rrIndex) == offsetof(MultisampleRange, rrCount) + 1,
              "rrCount and rrIndex must be adjacent");
#pragma GCC diagnostic pop
// Per-zone overhead when no alternates are loaded: pointer (4) + rrCount (1) + rrIndex (1) + rrMode (1)
// + lastResolvedSlotIndex (1) = 8 bytes (no padding needed).
static_assert(sizeof(MultisampleRange) <= sizeof(MultiRange) + sizeof(SampleHolderForVoice) + sizeof(void*) + 8,
              "MultisampleRange grew unexpectedly — check for unintended member additions");

MultisampleRange::MultisampleRange()
    : alternates(nullptr), rrCount(0), rrIndex(0), rrMode(RRMode::Cycle), lastResolvedSlotIndex(0) {
}

MultisampleRange::~MultisampleRange() {
	if (alternates != nullptr) {
		for (int32_t i = 0; i < kMaxRoundRobinAlternates; i++) {
			if (alternates->slots[i] != nullptr) {
				alternates->slots[i]->~SampleHolderForVoice();
				GeneralMemoryAllocator::get().dealloc(alternates->slots[i]);
			}
		}
		GeneralMemoryAllocator::get().dealloc(alternates);
	}
}

AudioFileHolder* MultisampleRange::getAudioFileHolder() {
	return static_cast<AudioFileHolder*>(&sampleHolder);
}

Error MultisampleRange::loadAllAudioFiles(bool reversed, bool mayActuallyReadFiles) {
	// Attempt every file even after an error, so one missing sample doesn't block the rest;
	// report the first error encountered.
	Error error = sampleHolder.loadFile(reversed, false, mayActuallyReadFiles, CLUSTER_ENQUEUE, nullptr, true);
	if (alternates != nullptr) {
		for (int32_t i = 0; i < rrCount; i++) {
			if (alternates->slots[i] != nullptr) {
				Error alternateError = alternates->slots[i]->loadFile(reversed, false, mayActuallyReadFiles,
				                                                      CLUSTER_ENQUEUE, nullptr, true);
				if (error == Error::NONE) {
					error = alternateError;
				}
			}
		}
	}
	return error;
}

void MultisampleRange::detachAllAudioFiles() {
	sampleHolder.setAudioFile(nullptr);
	if (alternates != nullptr) {
		for (int32_t i = 0; i < rrCount; i++) {
			if (alternates->slots[i] != nullptr) {
				alternates->slots[i]->setAudioFile(nullptr);
			}
		}
	}
}

RoundRobinAlternates* MultisampleRange::ensureAlternates() {
	if (alternates == nullptr) {
		void* mem = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(RoundRobinAlternates));
		if (mem == nullptr) {
			return nullptr;
		}
		alternates = new (mem) RoundRobinAlternates;
	}
	return alternates;
}

SampleHolderForVoice* MultisampleRange::ensureAlternateHolder(uint8_t alternateSlotIndex) {
	if (alternateSlotIndex >= kMaxRoundRobinAlternates) {
		return nullptr;
	}

	RoundRobinAlternates* alternateTable = ensureAlternates();
	if (alternateTable == nullptr) {
		return nullptr;
	}

	if (alternateTable->slots[alternateSlotIndex] == nullptr) {
		void* mem = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(SampleHolderForVoice));
		if (mem == nullptr) {
			return nullptr;
		}
		alternateTable->slots[alternateSlotIndex] = new (mem) SampleHolderForVoice();
	}

	return alternateTable->slots[alternateSlotIndex];
}

void MultisampleRange::clearAlternateSlot(uint8_t alternateSlotIndex) {
	if (!hasAlternateLoaded(alternateSlotIndex)) {
		return;
	}

	SampleHolderForVoice* holderToDelete = alternates->slots[alternateSlotIndex];
	holderToDelete->~SampleHolderForVoice();
	GeneralMemoryAllocator::get().dealloc(holderToDelete);

	for (uint8_t i = alternateSlotIndex; i + 1 < rrCount; i++) {
		alternates->slots[i] = alternates->slots[i + 1];
	}
	alternates->slots[rrCount - 1] = nullptr;
	rrCount--;

	uint8_t poolSize = rrCount + 1;
	if (rrIndex >= poolSize) {
		rrIndex = 0;
	}
	if (lastResolvedSlotIndex >= poolSize) {
		lastResolvedSlotIndex = 0;
	}
}

SampleHolderForVoice* MultisampleRange::getVariantHolder(uint8_t slotIndex) {
	if (slotIndex == 0) {
		return &sampleHolder;
	}
	if (slotIndex <= rrCount && alternates != nullptr) {
		return alternates->slots[slotIndex - 1];
	}
	return nullptr;
}

MultisampleRange* MultisampleRange::auditionRange_ = nullptr;
uint8_t MultisampleRange::auditionSlotIndex_ = 0;

SampleHolderForVoice* MultisampleRange::resolveVariant(uint8_t* resolvedSlotIndex) {
	uint8_t slotIndex;

	if (this == auditionRange_) [[unlikely]] {
		// A Variants menu session is auditioning a specific slot - play it without advancing the cycle.
		slotIndex = (auditionSlotIndex_ <= rrCount) ? auditionSlotIndex_ : 0;
	}
	else {
		switch (rrMode) {
		case RRMode::Random:
			slotIndex = resolveRandomSlotIndex(rrCount, rrIndex, (uint8_t)random(rrCount));
			break;
		case RRMode::NoRepeat:
			// random(n) returns [0, n], so random(rrCount-1) gives [0, rrCount-1] — the pool minus one slot.
			// When rrCount <= 1 there is no real choice; pass 0 directly to avoid random(0) ambiguity.
			slotIndex = resolveNoRepeatSlotIndex(rrCount, lastResolvedSlotIndex,
			                                     rrCount > 1 ? (uint8_t)random(rrCount - 1) : 0);
			break;
		default: // RRMode::Cycle
			slotIndex = resolveNextSlotIndex(rrCount, rrIndex);
			break;
		}
	}

	lastResolvedSlotIndex = slotIndex;
	if (resolvedSlotIndex != nullptr) {
		*resolvedSlotIndex = slotIndex;
	}

	SampleHolderForVoice* holder = getVariantHolder(slotIndex);
	if (holder == nullptr) {
		return &sampleHolder;
	}

	return holder;
}
