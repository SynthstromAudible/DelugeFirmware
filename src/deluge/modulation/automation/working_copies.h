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

#include <cstddef>
#include <cstdint>

class ModelStackWithAutoParam;
class ParamCollection;
class Song;

namespace deluge::project {
struct DocAutoParam;
}

/// The lanes the render records into: a working copy of each, which the render owns and writes while a clip records,
/// and the interaction tier copies into the model before it reads or edits lanes. The model's lanes are written by the
/// interaction tier alone; the render plays a lane's working copy while it has one, else its published snapshot.
///
/// The interaction tier makes, commits and releases copies, each with the render held off; the render only finds and
/// writes them. A copy is keyed by its collection's address and serial and the lane's paramId.
namespace deluge::modulation {

/// The lanes that can record at once.
inline constexpr size_t kWorkingCopies = 32;

#if ALPHA_OR_BETA_VERSION
/// Lanes not recorded into because the table was full or a copy could not be made.
extern uint32_t workingCopyOverflows;
/// Latch writes the render skipped on a lane with no working copy.
extern uint32_t latchWritesWithoutCopy;
#endif

/// @brief [render] The latch reached a lane with no working copy: it plays on and writes nothing.
void noteLatchWithoutCopy();

/// @brief [interaction] Make (or find) the working copy of @p lane: its model lane copied with room for @p room more
///        nodes, installed with the render held off. Making it first takes the lane's undo snapshot into the current
///        RECORD action, so whatever the render writes into the copy is undone with the take.
/// @param lane The lane, on the interaction tier.
/// @param room Nodes the copy has room to grow by without allocating.
/// @return The copy, or nullptr when the table is full or out of memory (counted).
project::DocAutoParam* makeWorkingCopy(ModelStackWithAutoParam const& lane, size_t room);

/// @brief [render] or [interaction] The working copy of lane @p paramId of @p collection.
/// @return It, or nullptr if the lane has none.
project::DocAutoParam* workingCopy(const ParamCollection& collection, int32_t paramId);

/// @brief [render] The render changed @p copy: the next commit copies it into the model.
void noteWorkingCopyWritten(const project::DocAutoParam& copy);

/// @brief [interaction] With the render held off, copy every working copy the render changed into its model lane,
///        and mark its collection for the next publish.
void commitWorkingCopies();

/// @brief [interaction] Commit, then release every working copy: freed once the render cannot be reading them.
void releaseWorkingCopies();

/// @brief [interaction] An edit of every lane of @p collection (shifting, trimming, repeating, deleting time…): the
/// take
///        ends for its lanes, so the copies are committed and released before the edit, and the next commit makes
///        copies again for those still overriding. On the render, which edits lanes this way only while arrangement
///        recording, nothing.
void endTake(ParamCollection& collection);

/// @brief [interaction] Whether a take ended for some lanes since the last call (endTake()).
bool takeEndedSinceLastAsked();

/// @brief [render] or [interaction] @p collection is going: its working copies are dropped and freed, uncommitted.
///        The render cannot reach a collection being destroyed, nor its copies.
void dropWorkingCopies(ParamCollection& collection);

/// @brief [interaction] Recording has started: make working copies for the lanes with nodes a knob is overriding,
///        which the latch writes as it drifts back.
/// @param song The song recording.
void copyOverriddenLanes(Song& song);

/// @brief [render] or [interaction] How many working copies are held.
size_t workingCopiesHeld();

/// How the table changes a collection's count of working copies.
class WorkingCopies {
public:
	/// @brief Add @p delta to @p collection's count.
	static void count(ParamCollection& collection, int32_t delta);
};

} // namespace deluge::modulation
