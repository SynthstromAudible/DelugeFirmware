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

#include "playback/trigger_clock_input.h"

#include <libdeluge/cv_gate.h>

namespace deluge::playback {

TriggerClockEdges triggerClockEdges;

namespace {
/// The BSP calls this once per rising edge, on its trigger-clock task. `stamp` is the edge's capture stamp,
/// widened to 64 bits; it fits in 32.
extern "C" void onTriggerClockEdge(uint64_t stamp) noexcept {
	triggerClockEdges.push(static_cast<uint32_t>(stamp));
}
} // namespace

void installTriggerClockInput() noexcept {
	deluge_trigger_clock_set_handler(&onTriggerClockEdge);
}

} // namespace deluge::playback
