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

#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "engine-api/types/param.h"
#include "engine-api/types/param_naming.h"

#include <cstdint>
#include <span>
#include <vector>

namespace deluge::project {

/// @brief One of a sound's mod knobs: knob @c knob (0 the lower, 1 the upper) of mod button @c button.
struct ModKnobControl {
	uint8_t button{0};
	uint8_t knob{0};
	[[nodiscard]] constexpr bool operator==(const ModKnobControl&) const = default;
};

/// @brief A mod knob and the param of its owner's engine it moves. A MIDI-learned knob is a binding of the song's
/// MappingTable.
struct ControllerMapping {
	ModKnobControl control;
	deluge::engine::ParamId target{};
	[[nodiscard]] bool operator==(const ControllerMapping&) const = default;
};

/// @brief Whose controls a mapping list is: an output's (a synth's sound, a kit's or an audio track's own learned
/// knobs), a sound kit row's, or the song's.
struct ControlsAddress {
	enum class Kind : uint8_t { OUTPUT, DRUM, SONG };
	Kind kind{Kind::SONG};
	uint32_t id{0}; ///< the output's or the drum's id; unused for the song
	bool operator==(const ControlsAddress&) const = default;
};

/// @brief The engines whose params the codecs may meet, with how each names them.
struct CodecEngines {
	std::span<const deluge::engine::ParamNaming> namings;
	/// How a host owner's effects are read and written; unset, the model keeps no chains.
	ChainCodec chains;
	/// How a sound's instrument is read and written; unset, the model keeps no instruments.
	InstrumentCodec instruments;
};

} // namespace deluge::project
