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

#include "instruments/classic/words.h"
#include "model/edits/setting_edit.h"
#include "processing/sound/sound.h"
#include "serialization/value.h"

#include <cstdint>
#include <string>

namespace deluge::test {

/// @brief Change @p sound's oscillator @p s to @p type as its oscillator-type menu does (the setting's Edit, or the
/// unslotted path for a sound in no slot): its record, its runtime and its ranges' kind change together.
inline void setOscType(::Sound& sound, int32_t s, deluge::instruments::classic::OscType type) {
	deluge::edits::settingFor(
	    sound, deluge::edits::oscSettingPath(s, "type"),
	    deluge::serialization::Value::makeScalar(std::string(deluge::instruments::classic::to_string(type))));
}

/// @brief Change @p sound's synth mode to @p mode as its synth-mode menu does.
inline void setSynthMode(::Sound& sound, deluge::instruments::classic::SynthMode mode) {
	deluge::edits::settingFor(
	    sound, "mode",
	    deluge::serialization::Value::makeScalar(std::string(deluge::instruments::classic::to_string(mode))));
}

} // namespace deluge::test
