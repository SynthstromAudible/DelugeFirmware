/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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
#include "modulation/params/param.h"

namespace deluge::modulation::params {
enum class ControlLayer { LIVE, CLIP, PRESET, REGION };

// Shared by panel reads/writes and MIDI feedback. Expression always remains a
// live input; recording and explicit automation edits never edit a shared preset.
// linked selects the shared preset layer; no caller links its controls yet.
constexpr ControlLayer controlLayer(Kind kind, bool hasScalar, bool editScalar, bool linked, bool region = false) {
	if (region) {
		return ControlLayer::REGION;
	}
	if (!hasScalar || kind == Kind::EXPRESSION || !editScalar) {
		return ControlLayer::LIVE;
	}
	return linked ? ControlLayer::PRESET : ControlLayer::CLIP;
}

} // namespace deluge::modulation::params
