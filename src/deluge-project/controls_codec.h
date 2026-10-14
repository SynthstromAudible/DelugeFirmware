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

#include "deluge-project/controller_mapping.h"
#include "engine-api/types/param_naming.h"
#include "serialization/value.h"

#include <vector>

namespace deluge::project {

/// @brief The controls an output's, a drum's or the song's @p element holds: its mod knobs (`modKnobs`, a sound's,
/// knob 0 then 1 of each button in turn), then its learned MIDI knobs (`midiKnobs`), each pointed at the param
/// @p naming gives its target. A knob whose target the naming cannot name is left out (a mod knob still takes its
/// place); a mod knob's target follows the naming's mod-knob rule.
[[nodiscard]] std::vector<ControllerMapping> decode_controls(const serialization::Value& element,
                                                             const deluge::engine::ParamNaming& naming);

/// @brief Write @p controls into @p element as decode_controls reads them: `modKnobs` when there is a mod knob, then
/// `midiKnobs` when there is a learned knob.
void encode_controls(const std::vector<ControllerMapping>& controls, const deluge::engine::ParamNaming& naming,
                     serialization::Value& element);

} // namespace deluge::project
