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

#pragma once

#include "model/sample/sample_holder_for_voice.h"
#include "storage/multi_range/multi_range.h"

class Source;
class Sample;
class Cluster;

/// Maximum number of round-robin alternate samples per zone (slot 0 is sampleHolder, slots 1-3 are alternates).
static constexpr uint8_t kMaxRoundRobinAlternates = 3;

/// Lazily-allocated table of pointers to alternate SampleHolderForVoice objects.
/// The table is allocated when the first alternate is loaded; each individual alternate holder
/// is then allocated separately on demand.
struct RoundRobinAlternates {
	SampleHolderForVoice* slots[kMaxRoundRobinAlternates] = {nullptr, nullptr, nullptr};
};

class MultisampleRange final : public MultiRange {
public:
	MultisampleRange();
	~MultisampleRange() override;

	// This class is intentionally memmove-relocatable (the array it lives in uses memmove, not placement-new).
	// C++ copy semantics would shallow-copy the `alternates` raw pointer and create a double-free.
	MultisampleRange(const MultisampleRange&) = delete;
	MultisampleRange& operator=(const MultisampleRange&) = delete;

	AudioFileHolder* getAudioFileHolder() override;
	Error loadAllAudioFiles(bool reversed, bool mayActuallyReadFiles) override;
	void detachAllAudioFiles() override;

	/// Returns a pointer to the SampleHolderForVoice for the given slot index (0 = primary, 1-3 = alternates).
	/// Returns nullptr if the slot is unpopulated or out of range.
	SampleHolderForVoice* getVariantHolder(uint8_t slotIndex);

	// Static overloads: pure predicates, testable without constructing a MultisampleRange.
	static bool hasAlternateLoaded(uint8_t alternateSlotIndex, uint8_t count, const RoundRobinAlternates* alts) {
		return alternateSlotIndex < count && alts != nullptr && alts->slots[alternateSlotIndex] != nullptr;
	}
	static uint8_t getNextAlternateSlotToPopulate(uint8_t count) { return (std::min)(count, kMaxRoundRobinAlternates); }
	static bool canPopulateAlternateSlot(uint8_t alternateSlotIndex, uint8_t count) {
		return alternateSlotIndex < kMaxRoundRobinAlternates && alternateSlotIndex <= count;
	}

	bool hasAlternateLoaded(uint8_t alternateSlotIndex) const {
		return hasAlternateLoaded(alternateSlotIndex, rrCount, alternates);
	}
	uint8_t getNextAlternateSlotToPopulate() const { return getNextAlternateSlotToPopulate(rrCount); }
	bool canPopulateAlternateSlot(uint8_t alternateSlotIndex) const {
		return canPopulateAlternateSlot(alternateSlotIndex, rrCount);
	}
	void clearAlternateSlot(uint8_t alternateSlotIndex);
	/// Computes which slot to play next and advances rrIndex in read-then-advance order.
	/// rrCount is number of alternates (0..3), so pool size is rrCount + 1.
	static uint8_t resolveNextSlotIndex(uint8_t rrCount, uint8_t& rrIndex) {
		if (rrCount == 0) {
			rrIndex = 0;
			return 0;
		}

		uint8_t poolSize = rrCount + 1;
		if (rrIndex >= poolSize) {
			rrIndex = 0;
		}

		uint8_t slotIndex = rrIndex;
		rrIndex++;
		if (rrIndex >= poolSize) {
			rrIndex = 0;
		}

		return slotIndex;
	}
	// --- Menu audition override ---
	// Static because at most one menu editing session exists at a time. While set, resolveVariant()
	// on the matching range plays the given slot without advancing the cycle, so auditioning from a
	// slot's menu always plays the slot being edited. The stored pointer is only ever compared,
	// never dereferenced, so a stale value cannot crash - but callers should still clear it when
	// their menu session ends.
	static void setAuditionSlot(MultisampleRange* range, uint8_t slotIndex) {
		auditionSlotIndex_ = slotIndex;
		auditionRange_ = range;
	}
	static void clearAuditionSlot() { auditionRange_ = nullptr; }

	/// Resolve the next variant to play using read-then-advance round robin.
	/// If there is only one active slot, this returns &sampleHolder immediately.
	SampleHolderForVoice* resolveVariant(uint8_t* resolvedSlotIndex = nullptr);

	/// Ensure the alternates pointer table is allocated and return it. Returns nullptr on allocation failure.
	RoundRobinAlternates* ensureAlternates();
	/// Ensure one alternate holder is allocated and return it. alternateSlotIndex is 0..2.
	SampleHolderForVoice* ensureAlternateHolder(uint8_t alternateSlotIndex);

	enum class RRMode : uint8_t { Cycle = 0, Random = 1, NoRepeat = 2 };

	/// Picks a random slot from [0, rrCount], updating rrIndex.
	/// randomSlot must be in [0, rrCount] (caller passes random(rrCount)).
	static uint8_t resolveRandomSlotIndex(uint8_t rrCount, uint8_t& rrIndex, uint8_t randomSlot) {
		uint8_t poolSize = rrCount + 1;
		uint8_t slotIndex = poolSize > 1 ? randomSlot % poolSize : 0;
		rrIndex = slotIndex;
		return slotIndex;
	}

	/// Picks a random slot from [0, rrCount] excluding lastResolved, ensuring no immediate repeat.
	/// randomChoice must be in [0, rrCount-1] (caller passes random(rrCount-1)).
	/// When poolSize == 1, always returns 0.
	static uint8_t resolveNoRepeatSlotIndex(uint8_t rrCount, uint8_t lastResolved, uint8_t randomChoice) {
		uint8_t poolSize = rrCount + 1;
		if (poolSize <= 1) {
			return 0;
		}
		uint8_t pick = randomChoice % (poolSize - 1);
		if (pick >= lastResolved) {
			pick++;
		}
		return pick;
	}

	// --- Memory layout notes ---
	// sampleHolder stays at offsetof(MultiRange)+padding = 8, identical to the pre-round-robin layout.
	// alternates pointer (4) + rrCount (1) + rrIndex (1) + rrMode (1) + lastResolvedSlotIndex (1) = 8 bytes per zone.
	// RoundRobinAlternates is a 12-byte pointer table. Each alternate holder (~88 bytes) is
	// allocated independently only when that specific slot is populated.

	SampleHolderForVoice sampleHolder;
	/// nullptr until the first alternate sample is loaded into this zone.
	RoundRobinAlternates* alternates;
	/// Number of populated slots in alternates->slots[] (0-3). 0 = only sampleHolder active (legacy).
	uint8_t rrCount;
	/// Next slot index to return (0 = sampleHolder, 1..rrCount = alternates). Read-then-advance.
	uint8_t rrIndex;
	/// Playback mode: Cycle (sequential), Random (any slot), NoRepeat (random, no immediate repeat).
	RRMode rrMode;
	/// Slot index that was last returned by resolveVariant(). Used by drawDrumName and NoRepeat exclusion.
	uint8_t lastResolvedSlotIndex;

private:
	/// See setAuditionSlot(). Compared against `this` in resolveVariant(); never dereferenced.
	static MultisampleRange* auditionRange_;
	static uint8_t auditionSlotIndex_;
};
