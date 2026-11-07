/*
 * Copyright © 2026 Rudement (Initial Author), Sean Ditny (Follow-up Author)
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

#include <algorithm>
#include <cstdint>
#include <optional>

enum class ActionType;
class Action;

namespace deluge::model {

struct AutomationRegion {
	uint32_t pos;
	uint32_t length;
	ActionType actionType;

	// Preserve the same timeline positions; do not wrap edits into shorter clips.
	uint32_t lengthWithin(int32_t loopLength) const {
		if (loopLength <= 0 || pos >= static_cast<uint32_t>(loopLength)) {
			return 0;
		}
		return std::min(length, static_cast<uint32_t>(loopLength) - pos);
	}
};

// An explicit user edit, not an automation playback value. A region carries its
// undo action type; no region means the normal live knob/menu/MIDI edit path.
struct ParameterEdit {
	int32_t value = 0;
	bool shouldLogAction = true;
	bool mayDeleteNodesInLinearRun = true;
	bool isMPE = false;
	std::optional<AutomationRegion> region;
	// For a region edit: rewrite each target's own value over the region instead of `value`, keeping the interpolation
	// of its own nodes around this position (see AutoParam::preserveRegion()).
	std::optional<uint32_t> preserveAround;
	// Delete each target's automation for this parameter, recording it into `action` so one undo restores every clip.
	bool deleteAutomation = false;
	Action* action = nullptr;
	// Whether the edited parameter was automated just after the edit. Taken before the edited clip is notified, which
	// can delete a patch cable and leave a different cable in its slot.
	bool sourceAutomated = false;
};

} // namespace deluge::model
