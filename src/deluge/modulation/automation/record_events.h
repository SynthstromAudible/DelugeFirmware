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

/// @file
/// @brief Values recorded while playing, handed from the interaction tier to the render, which writes them into the
/// lane: while a clip records, the render is the only writer of its lanes' nodes.
///
/// A knob, MIDI or MPE write while recording takes the undo snapshot on the interaction tier and posts an event with
/// what it read there (the play position, the clip's length and direction, how much ahead to clear); the render
/// applies the events at the start of its next block, before it ticks, so they land in the order they were made.
/// Before the interaction tier itself reads or edits lanes (a write, an Edit, undo, an automation delete, a recorded
/// note's MPE values) it writes what is still queued, with the render excluded, so it sees what it recorded.

#include "definitions_cxx.hpp"
#include "deluge-project/param_address.h"
#include "deluge-project/param_edit.h"
#include "deluge-project/param_node.h"
#include "model/snapshot/param_snapshot.h"
#include <cstddef>
#include <cstdint>

class ModelStackWithAutoParam;
class Song;

namespace deluge::modulation {

/// @brief An Edit made to a lane being recorded, which the render makes to its working copy in order with the values
/// recorded. Held by the interaction tier until the commit after the render has made it.
struct LaneCommand {
	enum class Kind : uint8_t {
		BASE,   ///< `value` becomes the base
		REGION, ///< `value` over [pos, pos + length), as project::setValueForRegion with `region`
		CLEAR,  ///< every node deleted
		PASTE,  ///< `pasted` over [pos, endPos), scaled by `scale`, as project::pasteRegion over `loopLength`
	};
	Kind kind{Kind::BASE};
	int32_t value{0};
	int32_t pos{0};
	int32_t length{0};
	int32_t endPos{0};
	int32_t loopLength{0};
	float scale{1.0F};
	bool isPatchCable{false};
	snapshot::RegionContext region{};
	project::CopiedAutomation pasted;
};

/// @brief A value recorded into a lane while playing, with every input the live-record kernel reads that is not the
/// lane's own nodes; or, with @ref command, an Edit's change to a lane being recorded.
struct RecordEvent {
	const ::Song* song{nullptr}; ///< the song @ref lane is addressed in
	project::ParamAddress lane{};
	int32_t livePos{0};
	int32_t value{0};
	int32_t ticksToClear{0};    ///< the stretch ahead of livePos the write clears
	int32_t effectiveLength{0}; ///< the clip's loop length
	int32_t posAtWhichPlaybackWillCut{0};
	uint32_t timePerInternalTick{0};
	bool reversed{false};
	bool recordingLinearly{false};
	bool mpe{false};
	/// The write may delete a node it leaves in a straight run: not MPE, an interpolating param, not reversed.
	bool deleteLinearRun{false};
	bool backtrackingCouldLoopBack{false};
	/// An Edit's change instead of a recorded value; owned by the interaction tier (see LaneCommand).
	const LaneCommand* command{nullptr};
};

/// @brief [task] The nodes one recorded write can add to its lane: the cleared region's two edges, and one the latch
/// may add where its override ends.
inline constexpr int32_t kNodesPerRecordedWrite = 3;

/// @brief [task] Make room in @p nodes for a recorded write and for every write still queued, so the render does not
/// grow the vector when it writes them. With the render excluded.
void reserveForRecordedWrite(project::ParamNodeVector& nodes);

/// @brief [task] An Edit made @p command's change to @p lane in the model: if the lane is being recorded, the render
///        makes it to its working copy too, in order with the values recorded. Room for @p room more nodes is made in
///        the copy first.
/// @param lane    The lane, on the interaction tier.
/// @param command The change, held until the render has made it.
/// @param room    Nodes the change can add.
void postLaneCommand(ModelStackWithAutoParam const& lane, LaneCommand command, size_t room);

/// @brief [task] Queue @p event for the render.
/// @return false when the queue is full: the caller, with the render excluded, writes the queue and then its event.
[[nodiscard]] bool postRecordEvent(const RecordEvent& event);

/// @brief [audio] Write the queued events into their lanes in @p song, oldest first; an event made in another song, or
/// whose lane no longer resolves (its clip was deleted after the write), is dropped. Bounded: at most the queue's
/// capacity per call. [task] only with the render excluded, which makes it the queue's one consumer while it runs.
void applyRecordEvents(::Song& song);

/// @brief [task] Write what is still queued into the working copies, with the render excluded, then copy every working
/// copy the render changed into the model (modulation/automation/working_copies.h); once recording has stopped, release
/// them. Before every job, and before the interaction tier reads or edits lanes, so it sees every value it recorded.
void commitRecording(::Song& song);

/// @brief [task] @p song is going: write the queue into the current song if that is another, else drop it, so no event
/// outlives the song its lane is addressed in.
void forgetRecordEvents(const ::Song& song);

#if ALPHA_OR_BETA_VERSION
/// Recorded values the interaction tier wrote itself because their lane has no address to queue them by.
extern uint32_t recordWritesWithoutAddress;
/// Writes applied by the interaction tier because the queue was full.
extern uint32_t recordRingFullFallbacks;
/// Writes found in the unheld lane when the render next reached it (each undone): it must stay 0.
extern uint32_t unheldLaneWrites;
/// Lanes the interaction tier resolved by address to another lane than the one the render's lookup finds: it must
/// stay 0.
extern uint32_t laneSplits;
/// `ActionLogger::getNewAction` and `Action::recordParamChange` calls made on the render tier.
extern uint32_t renderUndoCalls;
/// Recorded writes the render had to grow a lane's node vector for: the reserved room ran out.
extern uint32_t renderNodeGrowths;
#endif

} // namespace deluge::modulation
