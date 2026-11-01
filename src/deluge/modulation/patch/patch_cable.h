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

#include "modulation/automation/auto_param.h"
#include "synth/modulation/cable.h"
#include "util/fixedpoint.h"
#include <cstdint>

class Sound;
class ParamManagerForTimeline;
class InstrumentClip;

enum class Polarity : uint8_t {
	UNIPOLAR = 0,
	BIPOLAR = 1,
};

Polarity stringToPolarity(std::string_view string);
std::string_view polarityToString(Polarity polarity);
std::string_view polarityToStringShort(const Polarity polarity);

class PatchCable {
public:
	PatchCable() = default;
	void setDefaultPolarity();
	static bool hasPolarity(PatchSource source);
	static Polarity getDefaultPolarity(PatchSource source);
	PatchSource from{PatchSource::NONE};
	Polarity polarity{Polarity::BIPOLAR};
	deluge::synth::CableTarget destination{0}; ///< an unused cable slot holds word 0
	/// The depth's interpolation cursor, within [-2^30, 2^30]; the depth's value and automation are the host's lane,
	/// PatchCableSet::docForParam.
	AutoParam param;
};
