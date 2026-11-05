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
#include "io/debug/log.h"
#include "libdeluge/system.h"
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "util/spsc_ring.h"
#include <algorithm>
#include <cstddef>

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

// [audio], or [task] with the render excluded: then the interaction tier is the only consumer while it runs.
void applyRecordEvents(::Song& song) {
	RecordEvent event;
	for (std::size_t n = 0; n < kRecordEventCapacity && g_recordEvents.try_pop(event); n++) {
		if (event.song != &song) {
			continue;
		}
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		if (ModelStackWithAutoParam* stack = bridge::paramStackOnRender(song, event.lane, memory)) {
			project::DocAutoParam& lane = *stack->doc();
			if (isUnheldLane(lane)) {
				continue;
			}
#if ALPHA_OR_BETA_VERSION
			const size_t capacity = lane.nodes.capacity();
#endif
			stack->autoParam->writeRecordedValue(lane, event);
#if ALPHA_OR_BETA_VERSION
			if (lane.nodes.capacity() != capacity) {
				++renderNodeGrowths;
			}
#endif
		}
	}
}

void drainRecordEvents(::Song& song) {
	if (g_recordEvents.empty()) {
		return;
	}
	AudioExcludedSection section;
	applyRecordEvents(song);
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
