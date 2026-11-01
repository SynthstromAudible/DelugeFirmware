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
#include "plugin-api/types/param_naming.h"
#include "serialization/value.h"

#include <vector>

namespace deluge::project {

/// @brief The controls a sound's @p element holds: its mod knobs (`modKnobs`, knob 0 then 1 of each button in turn),
/// each pointed at the param @p naming gives its target. A knob whose target the naming cannot name is left out (it
/// still takes its place); a knob's target follows the naming's mod-knob rule. Learned MIDI knobs are bindings
/// (mapping_codec).
[[nodiscard]] std::vector<ControllerMapping> decode_controls(const serialization::Value& element,
                                                             const deluge::plugin::ParamNaming& naming);

/// @brief Write @p controls into @p element as decode_controls reads them: `modKnobs` when there is a mod knob.
void encode_controls(const std::vector<ControllerMapping>& controls, const deluge::plugin::ParamNaming& naming,
                     serialization::Value& element);

} // namespace deluge::project
