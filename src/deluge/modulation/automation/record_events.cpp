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
#include "model/model_stack.h"
#include "model/project_bridge/param_stack.h"
#include "modulation/automation/auto_param.h"
#include "util/spsc_ring.h"
#include <cstddef>

namespace deluge::modulation {

#if ALPHA_OR_BETA_VERSION
uint32_t recordWritesOutsideExecutor = 0;
uint32_t recordRingFullFallbacks = 0;
uint32_t renderUndoCalls = 0;
#endif

namespace {
/// Room for the values one interaction-tier pass records between two render blocks.
constexpr std::size_t kRecordEventCapacity = 64;
util::SpscRing<RecordEvent, kRecordEventCapacity> g_recordEvents;
} // namespace

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
			stack->autoParam->writeRecordedValue(*stack->doc(), event);
		}
	}
}

} // namespace deluge::modulation
