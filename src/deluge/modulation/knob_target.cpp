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

#include "modulation/knob_target.h"

#include "instruments/deluge/cables.h"
#include "model/project_bridge/param_ids.h"

namespace deluge::modulation {

deluge::synth::CableTarget knobWord(const KnobTarget& target) {
	if (!target) {
		return {};
	}
	if (deluge::instrument::is_cable_depth(target->id)) {
		return {deluge::bridge::cableWordOf(target->id)};
	}
	return deluge::synth::CableTarget::param(static_cast<uint8_t>(target->id.value));
}

KnobTarget knobTargetOf(deluge::synth::CableTarget word) {
	if (word.is_null()) {
		return std::nullopt;
	}
	if (word.is_param()) {
		return knobParam(word.param_index());
	}
	return deluge::project::engine_target(deluge::bridge::cableDepthIdOf(word.word).value);
}

} // namespace deluge::modulation
