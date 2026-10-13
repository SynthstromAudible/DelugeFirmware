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
/// Number of round-robin slots including the primary (slot 0) and up to 3 alternates (slots 1-3).
static constexpr uint8_t kMaxRoundRobinSlots = kMaxRoundRobinAlternates + 1;

/// Lazily-allocated per-zone storage for round-robin alternates and, since RRMode::Velocity, their
/// shared per-slot velocity ranges. The table is allocated when the first alternate is loaded, or
/// when a velocity range is first edited away from its default (see
/// MultisampleRange::setVelocityRange()); each individual alternate holder is then allocated
/// separately on demand.
///
/// `slots[]` holds pointers to the *alternate* SampleHolderForVoice objects only - index i is
/// slotIndex i+1, since the primary sample's own holder lives directly on MultisampleRange as
/// `sampleHolder` and is never stored here.
///
/// `velocityRangeMin`/`velocityRangeMax`, by contrast, index all 4 slots directly (array index ==
/// slotIndex, including the primary at index 0), so RRMode::Velocity resolution can look up any
/// slot's range the same way. That makes "Alternates" a slight misnomer once Velocity mode is in
/// play - this struct now also carries the primary slot's own range - but it's kept here, rather
/// than added as a new field on MultisampleRange, because this struct is already lazily allocated
/// exactly when a zone starts actually using round-robin. That's what keeps MultisampleRange's own
/// baseline size (paid by every multisampled zone, round-robin or not) unchanged - see
/// static_asserts in multisample_range.cpp.
struct RoundRobinAlternates {
	SampleHolderForVoice* slots[kMaxRoundRobinAlternates] = {nullptr, nullptr, nullptr};
	/// Per-slot velocity range for RRMode::Velocity, 1-127 inclusive. Defaults to the full range so
	/// an unconfigured slot always matches, mirroring note::Velocity / defaults::Velocity's 1-127
	/// convention (see gui/menu_item/note/velocity.h, gui/menu_item/defaults/velocity.h).
	uint8_t velocityRangeMin[kMaxRoundRobinSlots] = {1, 1, 1, 1};
	uint8_t velocityRangeMax[kMaxRoundRobinSlots] = {127, 127, 127, 127};
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
	/// Shifts each velocity range down to follow the sample that clearAlternateSlot() just moved
	/// into its position, and resets the vacated top slot to the full default range. The ranges are
	/// indexed by slotIndex while the sample pointers are indexed by alternate index, so they need
	/// compacting separately - transpose/cents/volume live on the holder and travel with it for
	/// free. `alternateSlotIndex` is 0-based among alternates; `rrCount` is the count BEFORE the
	/// clear. Static and pure so the index arithmetic is testable on the host.
	static void compactVelocityRangesAfterClear(RoundRobinAlternates& alts, uint8_t alternateSlotIndex,
	                                            uint8_t rrCount) {
		for (uint8_t slotIndex = alternateSlotIndex + 1; slotIndex + 1 <= rrCount; slotIndex++) {
			alts.velocityRangeMin[slotIndex] = alts.velocityRangeMin[slotIndex + 1];
			alts.velocityRangeMax[slotIndex] = alts.velocityRangeMax[slotIndex + 1];
		}
		alts.velocityRangeMin[rrCount] = kDefaultVelocityMin;
		alts.velocityRangeMax[rrCount] = kDefaultVelocityMax;
	}
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
	//
	// The pointer identifies its range by address, and ranges live in a MultiRangeArray that moves
	// its elements on insert/delete and rebuilds them entirely on changeType(). A pointer left over
	// across one of those can therefore compare equal to a *different* range, which would then be
	// pinned to one slot and stop cycling - so those three call sites clear it too, not just the
	// menu ones. Reaching one of them from a live slot menu is not hypothetical: the slot's FILE
	// item opens the sample browser over the top of the menu without ending the session, and a
	// whole-folder import from there deletes and re-inserts every range.
	static void setAuditionSlot(MultisampleRange* range, uint8_t slotIndex) {
		auditionSlotIndex_ = slotIndex;
		auditionRange_ = range;
	}
	static void clearAuditionSlot() { auditionRange_ = nullptr; }

	/// Resolve the next variant to play using read-then-advance round robin (or, in Velocity mode,
	/// by matching the incoming note's velocity against each slot's configured range).
	/// If there is only one active slot, this returns &sampleHolder immediately.
	SampleHolderForVoice* resolveVariant(uint8_t velocity, uint8_t* resolvedSlotIndex = nullptr);

	/// Ensure the alternates pointer table is allocated and return it. Returns nullptr on allocation failure.
	RoundRobinAlternates* ensureAlternates();
	/// Ensure one alternate holder is allocated and return it. alternateSlotIndex is 0..2.
	SampleHolderForVoice* ensureAlternateHolder(uint8_t alternateSlotIndex);

	// Serialized as a raw integer in song XML (see sound.cpp), so existing values must never be
	// renumbered - only appended to.
	enum class RRMode : uint8_t { Cycle = 0, Random = 1, NoRepeat = 2, Velocity = 3 };

	static constexpr uint8_t kDefaultVelocityMin = 1;
	static constexpr uint8_t kDefaultVelocityMax = 127;

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

	/// Picks the slot to play from those whose configured [min, max] velocity range contains
	/// `velocity`, the way MPC's Velocity layer-play mode works. If none match (gaps between
	/// configured ranges are allowed, not an error), falls back to slot 0. `alts` may be nullptr
	/// (e.g. a hand-edited song claiming RRMode::Velocity with no alternates ever loaded) - defaults
	/// apply just as if every slot's range were unset.
	///
	/// Ranges are free to overlap, and on an MPC they usually do: every layer starts at the full
	/// range, and overlapping a little (1-45 against 40-85) is the normal way to soften the step
	/// between layers. There a shared band plays both layers stacked; a Deluge voice has one sample
	/// per oscillator, so instead the band's slots are round-robined between - velocity picks the
	/// band, and the cycle picks the take within it. Two slots on 1-64 and two on 65-127 gives two
	/// velocity layers of two alternating takes each. Where nothing overlaps exactly one slot ever
	/// matches, so this is the plain velocity switch it has always been.
	///
	/// `rrIndex` is a position within the matching band rather than a slot number, and is shared
	/// with Cycle mode's own index. Moving to a band with fewer slots in it simply wraps, the same
	/// way resolveNextSlotIndex() wraps when rrCount shrinks.
	///
	/// velocity == 128 is an internal "max" sentinel Voice::noteOn uses for the VELOCITY patch
	/// source (see voice.cpp) - never a genuine pad-hit or MIDI velocity - so it's clamped to 127
	/// before matching; otherwise it would fail to fall inside any slot's range, since ranges are
	/// capped at 127.
	static uint8_t resolveVelocitySlotIndex(uint8_t rrCount, uint8_t velocity, const RoundRobinAlternates* alts,
	                                        uint8_t& rrIndex) {
		uint8_t clampedVelocity = (velocity > kDefaultVelocityMax) ? kDefaultVelocityMax : velocity;

		uint8_t matches[kMaxRoundRobinSlots];
		uint8_t numMatches = 0;
		for (uint8_t slotIndex = 0; slotIndex <= rrCount && slotIndex < kMaxRoundRobinSlots; slotIndex++) {
			uint8_t min = (alts != nullptr) ? alts->velocityRangeMin[slotIndex] : kDefaultVelocityMin;
			uint8_t max = (alts != nullptr) ? alts->velocityRangeMax[slotIndex] : kDefaultVelocityMax;
			if (clampedVelocity >= min && clampedVelocity <= max) {
				matches[numMatches++] = slotIndex;
			}
		}

		if (numMatches == 0) {
			return 0;
		}

		// Read-then-advance over the matching slots, same shape as resolveNextSlotIndex().
		if (rrIndex >= numMatches) {
			rrIndex = 0;
		}
		uint8_t slotIndex = matches[rrIndex];
		rrIndex++;
		if (rrIndex >= numMatches) {
			rrIndex = 0;
		}
		return slotIndex;
	}

	/// Returns slotIndex's configured velocity range, or the full 1-127 default if that slot's
	/// range was never explicitly set (including every slot of a zone that has never allocated
	/// `alternates` at all).
	uint8_t getVelocityRangeMin(uint8_t slotIndex) const {
		return (alternates != nullptr) ? alternates->velocityRangeMin[slotIndex] : kDefaultVelocityMin;
	}
	uint8_t getVelocityRangeMax(uint8_t slotIndex) const {
		return (alternates != nullptr) ? alternates->velocityRangeMax[slotIndex] : kDefaultVelocityMax;
	}

	/// Sets slotIndex's velocity range, lazily allocating `alternates` if this zone has never
	/// populated an alternate or customized a range before. Returns false on allocation failure.
	bool setVelocityRange(uint8_t slotIndex, uint8_t min, uint8_t max);

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
