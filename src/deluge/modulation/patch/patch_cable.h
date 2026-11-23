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

#include "dsp/core/fixed_point.h"
#include "instruments/classic/cables.h"
#include "modulation/automation/auto_param.h"
#include "synth/modulation/cable.h"
#include <cstdint>

class Sound;
class ParamManagerForTimeline;
class InstrumentClip;

using Polarity = deluge::instruments::classic::Polarity;

std::string_view polarityToString(Polarity polarity);

class PatchCable {
public:
	PatchCable() = default;
	void setDefaultPolarity();
	static Polarity getDefaultPolarity(PatchSource source);
	PatchSource from{PatchSource::NONE};
	Polarity polarity{Polarity::BIPOLAR};
	deluge::synth::CableTarget destination{0}; ///< an unused cable slot holds word 0
	/// The depth's interpolation cursor, within [-2^30, 2^30]; the depth's value and automation are the host's lane,
	/// PatchCableSet::docForParam.
	AutoParam param;

	/// @brief The cable's current depth, −1 to 1 (the cursor's word).
	[[nodiscard]] deluge::dsp::Q<30> depth() const { return deluge::dsp::Q<30>::from_raw(param.getCurrentValue()); }
};
