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

#include "modulation/automation/record_events.h"
#include "deluge-project/param_edit.h"
#include "io/debug/log.h"
#include "libdeluge/system.h"
#include "model/edits/param_address_for.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/automation/working_copies.h"
#include "modulation/params/lane_publication.h"
#include "playback/playback_handler.h"
#include "util/spsc_ring.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace deluge::modulation {

#if ALPHA_OR_BETA_VERSION
uint32_t recordWritesWithoutAddress = 0;
uint32_t unheldLaneWrites = 0;
uint32_t laneSplits = 0;
uint32_t recordRingFullFallbacks = 0;
uint32_t renderUndoCalls = 0;
uint32_t renderNodeGrowths = 0;
#endif

namespace {
/// Room for the values one interaction-tier pass records between two render blocks.
constexpr std::size_t kRecordEventCapacity = 64;
util::SpscRing<RecordEvent, kRecordEventCapacity> g_recordEvents;

/// [task] The Edits' changes queued, freed by the commit after the render made them.
std::vector<std::unique_ptr<LaneCommand>> g_commands;

/// Make @p command's change to @p lane, a working copy.
void applyCommand(project::DocAutoParam& lane, const LaneCommand& command) {
	switch (command.kind) {
	case LaneCommand::Kind::BASE:
		project::set_base(lane, command.value);
		break;
	case LaneCommand::Kind::REGION: {
		const snapshot::RegionContext& r = command.region;
		(void)project::setValueForRegion(lane, static_cast<uint32_t>(command.pos),
		                                 static_cast<uint32_t>(command.length), command.value, r.effectiveLength,
		                                 r.interpolateLeftNode, r.interpolateRightNode, r.recordingLinearly,
		                                 r.clockActive, r.livePos, r.currentlyPlayingReversed);
		break;
	}
	case LaneCommand::Kind::CLEAR:
		lane.nodes.clear();
		break;
	case LaneCommand::Kind::PASTE:
		project::pasteRegion(lane, command.pos, command.endPos, command.scale, command.loopLength, command.isPatchCable,
		                     command.pasted);
		break;
	}
}
} // namespace

void reserveForRecordedWrite(project::ParamNodeVector& nodes) {
	const auto room = static_cast<size_t>(kNodesPerRecordedWrite) * (g_recordEvents.size() + 1);
	if (nodes.capacity() < nodes.size() + room) {
		// Grown by at least its size, so a run of writes copies the nodes a logarithmic number of times.
		if (!nodes.reserveExtra(static_cast<int32_t>(std::max(room, nodes.size())))) {
			D_PRINTLN("no room reserved for a recorded write: the render grows the lane");
		}
	}
}

bool postRecordEvent(const RecordEvent& event) {
	return g_recordEvents.try_push(event);
}

void postLaneCommand(ModelStackWithAutoParam const& lane, LaneCommand command, size_t room) {
	project::DocAutoParam* copy = workingCopy(*lane.paramCollection, lane.paramId);
	if (copy == nullptr) {
		return;
	}
	g_commands.push_back(std::make_unique<LaneCommand>(std::move(command)));
	RecordEvent event{.song = lane.song, .command = g_commands.back().get()};
	const std::optional<project::ParamAddress> address = edits::addressFor(lane);
	AudioExcludedSection section;
	reserveForRecordedWrite(copy->nodes);
	if (copy->nodes.capacity() < copy->nodes.size() + room) {
		(void)copy->nodes.reserveExtra(static_cast<int32_t>(room));
	}
	if (address) {
		event.lane = *address;
	}
	if (!address || !postRecordEvent(event)) {
		// The render is excluded: make what it has queued, then this, so the changes land in order
		applyRecordEvents(*lane.song);
		applyCommand(*copy, *event.command);
		noteWorkingCopyWritten(*copy);
	}
}

// [audio], or [task] with the render excluded: then the interaction tier is the only consumer while it runs.
void applyRecordEvents(::Song& song) {
	RecordEvent event;
	for (std::size_t n = 0; n < kRecordEventCapacity && g_recordEvents.try_pop(event); n++) {
		if (event.song != &song) {
			continue;
		}
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* stack = bridge::paramStackOnRender(song, event.lane, memory);
		project::DocAutoParam* lane =
		    (stack != nullptr) ? workingCopy(*stack->paramCollection, stack->paramId) : nullptr;
		if (lane == nullptr) {
			continue; // the take was released before the render reached the value
		}
#if ALPHA_OR_BETA_VERSION
		const size_t capacity = lane->nodes.capacity();
#endif
		if (event.command != nullptr) {
			applyCommand(*lane, *event.command);
		}
		else {
			stack->autoParam->writeRecordedValue(*lane, event);
		}
		noteWorkingCopyWritten(*lane);
#if ALPHA_OR_BETA_VERSION
		if (lane->nodes.capacity() != capacity) {
			++renderNodeGrowths;
		}
#endif
	}
}

void commitRecording(::Song& song) {
	const bool recording = playbackHandler.recording != RecordingMode::OFF;
	static bool takeOpen = false;
	// The latch may drift back on a lane a knob was turning when recording began, or when its take ended
	if (recording && (!takeOpen || takeEndedSinceLastAsked())) {
		copyOverriddenLanes(song);
	}
	takeOpen = recording;
	if (g_recordEvents.empty() && workingCopiesHeld() == 0) {
		return;
	}
	{
		AudioExcludedSection section;
		applyRecordEvents(song);
	}
	g_commands.clear(); // every queued Edit's change has been made
	if (recording) {
		commitWorkingCopies();
	}
	else {
		releaseWorkingCopies(); // the take is over: its lanes play from the model's published snapshots
	}
}

void forgetRecordEvents(const ::Song& song) {
	AudioExcludedSection section;
	if (currentSong != nullptr && currentSong != &song) {
		applyRecordEvents(*currentSong);
	}
	RecordEvent event;
	while (g_recordEvents.try_pop(event)) {}
}

} // namespace deluge::modulation
