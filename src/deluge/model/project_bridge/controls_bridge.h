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
#include "engine-api/types/param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_descriptor.h"

#include <vector>

class ModControllableAudio;
class Sound;
class Song;

namespace deluge::bridge {

/// @brief The param a runtime knob's descriptor points at: a plain param through its engine's map (@p unpatchedKind
/// numbers an unpatched one), a cable's depth through the instrument's depth ids.
[[nodiscard]] deluge::engine::ParamId knobTargetOf(ParamDescriptor descriptor,
                                                   deluge::modulation::params::Kind unpatchedKind);

/// @brief A sound's controls as the model holds them: its 16 mod knobs (button by button, knob 0 then 1), then its
/// learned MIDI knobs.
[[nodiscard]] std::vector<deluge::project::ControllerMapping> controlsOf(::Sound& sound);

/// @brief A global effectable's controls (a kit's, an audio track's, the song's): its learned MIDI knobs.
[[nodiscard]] std::vector<deluge::project::ControllerMapping> learnedControlsOf(::ModControllableAudio& owner);

/// @brief Set every output's, sound kit row's and the song's controls in the song's model from the runtime's knobs.
/// Run once a song is loaded and after each save, as the slots are filled and refreshed.
void refreshControls(::Song& song);

/// @brief The engines the app's model codecs name params through.
[[nodiscard]] deluge::project::CodecEngines codecEngines();

} // namespace deluge::bridge
