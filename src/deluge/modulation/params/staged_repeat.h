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

#include <atomic>
#include <cstdint>
#include <utility>

class InstrumentClip;
class ModelStackWithNoteRow;
class ModelStackWithTimelineCounter;
class NoteRow;
class Song;

/// An arrangement take's repeats. While the arrangement records a clip, its take repeats it at every loop end: the
/// render appends the repeat's notes and length there and counts it, and the interaction tier appends the repeat's
/// automation into the model before its next job, so the render neither writes a model lane nor makes a collection.
namespace deluge::modulation {

#if ALPHA_OR_BETA_VERSION
/// Loop ends of a take whose next repeat was not staged on every collection it appends to: the repeat played its old
/// lanes until the next job published them. Must stay 0.
extern uint32_t stagedRepeatMisses;
/// Loop ends at which the render swapped a take's staged repeat in.
extern uint32_t stagedRepeatSwaps;
/// Audio takes' loop ends that found no spare: the take did not go on. Must stay 0.
extern uint32_t audioSpareMisses;
#endif

/// A take's repeats while the arrangement records it, and its next repeat staged: a take clip's, or a row's of it with
/// its own length, which repeats at its own end.
struct TakeRepeats {
	TakeRepeats() = default;
	/// A copy starts with no repeats: a clip or row copied is not the take.
	TakeRepeats(const TakeRepeats& /*other*/) {}
	TakeRepeats& operator=(const TakeRepeats& /*other*/) { return *this = TakeRepeats{}; }
	/// A row moved within its vector, with the render held off, keeps its repeats.
	TakeRepeats(TakeRepeats&& other) noexcept { *this = std::move(other); }
	TakeRepeats& operator=(TakeRepeats&& other) noexcept {
		appended.store(other.appended.load(std::memory_order_relaxed), std::memory_order_relaxed);
		inModel = other.inModel;
		paramLength = other.paramLength;
		source.store(other.source.load(std::memory_order_relaxed), std::memory_order_relaxed);
		staged.store(other.staged.load(std::memory_order_relaxed), std::memory_order_relaxed);
		stagedCollections.store(other.stagedCollections.load(std::memory_order_relaxed), std::memory_order_relaxed);
		anyStaged = other.anyStaged;
		return *this;
	}

	std::atomic<uint32_t> appended{0}; ///< [render] or [interaction] counted at each repeat appended
	uint32_t inModel = 0;              ///< [interaction] the repeats whose automation the model holds
	int32_t paramLength = 0;           ///< [interaction] the length the model's automation covers
	/// [interaction] set, [render] or [interaction] cleared: the clip the take repeats, kept until the model holds
	/// every repeat counted; cleared when that clip goes. A row's repeats use its clip's.
	std::atomic<InstrumentClip*> source{nullptr};
	/// [interaction] set, [render] cleared: the next repeat is staged on every collection it appends to.
	std::atomic<bool> staged{false};
	/// [interaction] The collections the next repeat is staged on.
	std::atomic<uint32_t> stagedCollections{0};
	/// [interaction] Some collection of the take may hold a staged or superseded snapshot.
	bool anyStaged = false;
};

/// @brief [render] or [interaction] A repeat of @p take's source was appended to @p take (its notes and its length):
///        the interaction tier appends its automation at its next commit.
void countRepeat(InstrumentClip& take);
/// @brief [render] or [interaction] As countRepeat(), for @p row of a take, which has its own length.
void countRowRepeat(NoteRow& row);

/// @brief [interaction] Append into the model the automation of each repeat counted for each arrangement take of
///        @p song and not yet appended, oldest first, at the length the model's automation covers. First thing in
///        every commit of recording, so a working copy is committed before its lane is appended to.
void catchUpArrangementTakes(Song& song);

/// @brief [render] @p clip is going: no take repeats it any more, and a repeat counted for one is not appended.
void forgetRepeatSource(Song& song, const InstrumentClip& clip);

/// @brief [render] An audio take's loop end found no spare (counted).
void noteAudioSpareMissed();

/// @brief [interaction] Stage each recording take's next repeat where it is not staged, or was staged from source lanes
///        since republished; give each recording audio take a spare; and drop what is staged, and the spare, of takes
///        no longer recording. At the end of each job's publish.
void restageArrangementTakes(Song& song);

/// @brief [render] At @p take's loop end, publish its staged repeat: every collection the repeat appends to swaps in
///        its staged snapshot, or none does (counted).
/// @return Whether it swapped.
bool swapStagedRepeat(InstrumentClip& take, ModelStackWithTimelineCounter* thisModelStack,
                      ModelStackWithTimelineCounter* otherModelStack);
/// @brief [render] As swapStagedRepeat(), at the end of @p row of a take, which has its own length: @p rowModelStack
/// names
///        it, @p otherRowModelStack the source's row.
bool swapStagedRowRepeat(NoteRow& row, ModelStackWithNoteRow* rowModelStack, ModelStackWithNoteRow* otherRowModelStack);

#if defined(DELUGE_HOST)
/// @brief Host only: run @p hook on the render at each take's loop end, and at each of its rows' own end (@p row;
/// nullptr
///        for the clip's), once its repeat is appended and swapped in; nullptr clears it.
void setTakeLoopEndHook(void (*hook)(InstrumentClip& take, NoteRow* row));
/// @brief [render] Host only: run the hook set, if any, for @p take and @p row.
void takeLoopEnded(InstrumentClip& take, NoteRow* row = nullptr);
#else
inline void takeLoopEnded(InstrumentClip& /*take*/, NoteRow* /*row*/ = nullptr) {
}
#endif

} // namespace deluge::modulation
