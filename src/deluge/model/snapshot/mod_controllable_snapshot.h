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

namespace deluge::snapshot {

/// The render's copy of a ModControllableAudio's effect settings: the render reads nothing else of them. Replaced
/// whole, with the render excluded, whenever the owner's settings change (ModControllableAudio::setEffects).
struct ModControllableSnapshot {
	deluge::effects::EffectStates effects;
	/// A host chain's selected mod FX knob, which its soft off reads (GlobalEffectable::getActiveModFXType); set with
	/// the render excluded whenever the chain's view changes. A sound's is not read.
	ModFXParam mod_fx_param{ModFXParam::FEEDBACK};
};

} // namespace deluge::snapshot
