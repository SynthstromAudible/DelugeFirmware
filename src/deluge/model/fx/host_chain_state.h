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

#include "definitions_cxx.hpp"
#include "engines/effects/state.h"

namespace deluge::model {

/// @brief A host chain's settings as the engine side reads them: a kit's, an audio track's or the song master's
/// effects and the saved view (which value its mod FX knob and its filter knobs edit).
///
/// It is derived from the chain's FxChain record in the project model, the truth, as a Sound's SoundState is from its
/// slot: an Edit writes the record, then the state is set from what the record decodes to. Only an owner outside the
/// current song, which has no record, has its state written directly.
struct HostChainState {
	effects::EffectStates effects;
	ModFXParam mod_fx_param{ModFXParam::FEEDBACK};
	FilterType filter_type{FilterType::LPF};
	bool operator==(const HostChainState&) const = default;
};

} // namespace deluge::model
