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

/// The working copies held at once: lanes a take records or latches, and MPE lanes the render wrote since the last
/// commit.
inline constexpr size_t kWorkingCopies = 32;

#if ALPHA_OR_BETA_VERSION
/// Lanes not recorded into because the table was full or a copy could not be made.
extern uint32_t workingCopyOverflows;
/// Latch writes the render skipped on a lane with no working copy.
extern uint32_t latchWritesWithoutCopy;
#endif

/// @brief [render] The latch reached a lane with no working copy: it plays on and writes nothing.
void noteLatchWithoutCopy();

/// How long a working copy is held.
enum class Hold : uint8_t {
	TAKE,   ///< until the take ends: the latch may write it as it drifts back
	WRITES, ///< until a commit finds the render has not written it since the last (an MPE lane, which never latches)
};

/// @brief [interaction] Make (or find) the working copy of @p lane: its model lane copied with room for @p room more
///        nodes, installed with the render held off. Making it first takes the lane's undo snapshot into the current
///        RECORD action, so whatever the render writes into the copy is undone with the take.
/// @param lane The lane, on the interaction tier.
/// @param room Nodes the copy has room to grow by without allocating.
/// @param hold How long the copy is held; a copy found is held for the take if either asks for it.
/// @return The copy, or nullptr when the table is full or out of memory (counted).
project::DocAutoParam* makeWorkingCopy(ModelStackWithAutoParam const& lane, size_t room, Hold hold = Hold::TAKE);

/// @brief [render] or [interaction] The working copy of lane @p paramId of @p collection.
/// @return It, or nullptr if the lane has none.
project::DocAutoParam* workingCopy(const ParamCollection& collection, int32_t paramId);

/// @brief [render] The render changed @p copy: the next commit copies it into the model.
void noteWorkingCopyWritten(const project::DocAutoParam& copy);

/// @brief [interaction] With the render held off, copy every working copy the render changed into its model lane (a
///        copy whose take ended included), and mark its collection for the next publish. Then release the copies whose
///        take ended, and those held while written that the render did not write since the last commit.
void commitWorkingCopies();

/// @brief [interaction] Commit, then release every working copy: freed once the render cannot be reading them. The
///        next commit while recording makes copies again for the lanes still overriding.
void releaseWorkingCopies();

/// @brief [render] or [interaction] An edit of @p collection's lanes outside the record queue (shifting, trimming,
///        repeating, deleting time, moving a note's MPE…): the take ends for its lanes.
///
/// On the interaction tier, what is queued is written and the copies committed and released before the edit; the next
/// commit makes copies again for the lanes still overriding. On the render, which ends the take this way when an
/// arrangement take reaches its loop end, the render neither plays nor writes the copies from then on, and the next
/// commit copies what the render wrote into them into the model and frees them.
void endTake(ParamCollection& collection);

/// @brief [interaction] Whether a take ended for some lanes since the last call (endTake(), releaseWorkingCopies()).
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
