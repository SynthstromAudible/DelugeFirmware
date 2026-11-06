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

#include <cstdint>

class Song;
class ParamCollection;
class ParamManagerForTimeline;

/// Lanes as the render plays them. Outside record mode the render reads each collection's lanes from an immutable
/// snapshot the interaction tier publishes after every edit; it reads no model lane and writes none.
namespace deluge::modulation {

struct LaneSnapshot;

#if ALPHA_OR_BETA_VERSION
/// Lanes the render resolved through the model outside record mode: must stay 0.
extern uint32_t renderModelReads;
/// Collections whose published snapshot differed from the model after a publish (bits, order, base or nodes): must stay
/// 0.
extern uint32_t staleLanes;
/// Snapshots that could not be built (out of memory): the previous one stayed published.
extern uint32_t publishFailures;
/// Writes the render made to a lane outside record mode: must stay 0.
extern uint32_t renderPublishedWrites;
#endif

/// @brief [render] or [interaction] Whether the render reads lanes from the model as they are written: in record mode,
///        when the latch and recorded values may write any lane.
bool lanesLive();

/// @brief [render] or [interaction] Count a lane the render resolved through the model, if it should have read a
///        snapshot.
void noteRenderModelRead();

/// @brief [render] or [interaction] The snapshot a lane read of @p collection plays now, or nullptr to read the model.
///        The render (and a playhead move's collecting pass) plays the published lanes outside record mode, once the
///        collection has any; a collection not yet published is read in the model, and counted.
const LaneSnapshot* snapshotToPlay(const ParamCollection& collection);
/// @brief [render] or [interaction] Count a write to a lane made on the render outside record mode.
void noteRenderLaneWrite();

/// @brief [interaction] Compare every published collection's snapshot with its model: the same lanes, each with the
/// same
///        base and nodes.
/// @return The collections that differ.
int32_t checkPublished(Song& song);

/// @brief [interaction] @p collection's lanes may have changed: it republishes at the next publish point. Every path
///        that hands out a collection's lane to write, or rebinds its store, marks it. On the render it marks nothing:
///        the render reached a lane in the model, which is counted (noteRenderModelRead()).
void markLanesChanged(ParamCollection& collection);
/// @brief [interaction] Republish every collection marked changed whose lanes differ from its snapshot. A publish
/// point:
///        nothing happens on the render, or while a section holds the render off (the build allocates).
/// @param settle Whether the job is done with its lanes (the app loop, after each job): the marks are cleared. Within a
///        job (an edit's guard closing, a move's end) they stay, so a write through a lane still held is published at
///        the next point.
void publishDirtyLanes(bool settle = false);

/// @brief [interaction] Publish every collection of @p song before it is handed to the render: a song not yet current
///        is reached by no walk after an edit or a job.
/// @return False when out of memory: those collections are read in the model until a later walk publishes them.
bool publishSongLanes(Song& song);

/// @brief [interaction] A whole operation on the model -- an Edit applied, an undo or redo -- whose lanes the outermost
///        one publishes when it ends: between its steps a collection's storage may be mid-change.
class LaneOperation {
public:
	LaneOperation();
	~LaneOperation();
	LaneOperation(const LaneOperation&) = delete;
	LaneOperation& operator=(const LaneOperation&) = delete;
};

/// How the interaction tier publishes collections' lanes; the parts ParamCollection's private state is for.
class LanePublication {
public:
	/// @brief [render] or [interaction] A collection was made: the next publish point walks the song for it.
	static void made(ParamCollection& collection);
	/// @brief [render] or [interaction] A collection is going, out of the render's reach: its snapshot and lanes are
	///        freed.
	static void gone(ParamCollection& collection);
	/// @brief [interaction] As markLanesChanged().
	static void mark(ParamCollection& collection);
	/// @brief [interaction] As publishDirtyLanes().
	static void publishDirty(bool settle);
	/// @brief [interaction] As publishSongLanes().
	static bool publishSong(Song& song);
	/// @brief [interaction] As checkPublished().
	static int32_t check(Song& song);

private:
	/// @brief Publish @p collection if its lanes differ from its snapshot.
	/// @param manager The manager that schedules @p collection: its next event is brought forward to the next block.
	/// @return False when out of memory: the previous snapshot stays.
	static bool publish(ParamCollection& collection, ParamManagerForTimeline& manager);
	/// @brief Whether @p collection's snapshot holds exactly its lanes.
	static bool matches(ParamCollection& collection);
};

} // namespace deluge::modulation
