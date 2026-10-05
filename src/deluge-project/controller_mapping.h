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

#include "deluge-project/midi_command.h"
#include "engine-api/types/param.h"
#include "engine-api/types/param_naming.h"

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace deluge::project {

/// @brief One of a sound's mod knobs: knob @c knob (0 the lower, 1 the upper) of mod button @c button.
struct ModKnobControl {
	uint8_t button{0};
	uint8_t knob{0};
	[[nodiscard]] constexpr bool operator==(const ModKnobControl&) const = default;
};

/// @brief A MIDI-learned knob: the input it was learned from and whether it sends relative changes.
struct MidiInputControl {
	MidiCommand input;
	bool relative{false};
	[[nodiscard]] bool operator==(const MidiInputControl&) const = default;
};

/// @brief A control and the param of its owner's engine it moves.
struct ControllerMapping {
	std::variant<ModKnobControl, MidiInputControl> control;
	deluge::engine::ParamId target{};
	[[nodiscard]] bool operator==(const ControllerMapping&) const = default;
};

/// @brief The engines whose params the codecs may meet, with how each names them.
struct CodecEngines {
	std::span<const deluge::engine::ParamNaming> namings;
};

} // namespace deluge::project
