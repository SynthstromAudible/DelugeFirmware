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
#include "modulation/params/param.h"
#include "modulation/params/param_descriptor.h"
#include "plugin-api/types/param.h"

#include <optional>
#include <vector>

class ModControllable;
class Output;
class ModControllableAudio;
class Sound;
class Song;

namespace deluge::bridge {

/// @brief The param a runtime knob's descriptor points at: a plain param through its engine's map (@p unpatchedKind
/// numbers an unpatched one), a cable's depth through the instrument's depth ids.
/// @param descriptor    The knob's runtime descriptor.
/// @param unpatchedKind The Kind that numbers an unpatched param.
/// @return The target param id.
[[nodiscard]] deluge::plugin::ParamId knobTargetOf(ParamDescriptor descriptor,
                                                   deluge::modulation::params::Kind unpatchedKind);

/// @brief A sound's controls as the model holds them: its 16 mod knobs (button by button, knob 0 then 1), then its
/// learned MIDI knobs.
/// @param sound The sound.
/// @return The sound's controls.
[[nodiscard]] std::vector<deluge::project::ControllerMapping> controlsOf(::Sound& sound);

/// @brief A global effectable's controls (a kit's, an audio track's, the song's): its learned MIDI knobs.
/// @param owner The global effectable.
/// @return Its learned MIDI knobs as controls.
[[nodiscard]] std::vector<deluge::project::ControllerMapping> learnedControlsOf(::ModControllableAudio& owner);

/// @brief Set every output's, sound kit row's and the song's controls in the song's model from the runtime's knobs.
/// Run once a song is loaded and after each save, as the slots are filled and refreshed.
/// @param song The song.
void refreshControls(::Song& song);

/// @brief The runtime descriptor of a knob's target: the inverse of knobTargetOf (@p unpatchedKind numbers an unpatched
/// param, as there).
/// @param target        The model-side target.
/// @param unpatchedKind The Kind that numbers an unpatched param.
/// @return The runtime descriptor; null if @p target names no runtime param.
[[nodiscard]] ParamDescriptor descriptorOf(deluge::plugin::ParamId target,
                                           deluge::modulation::params::Kind unpatchedKind);

/// @brief Whose controls @p sound's are: its synth track's or its kit row's; nullopt when it is not bound.
/// @param sound The sound.
/// @return The controls address, or nullopt.
[[nodiscard]] std::optional<deluge::project::ControlsAddress> controlsAddressOf(::Sound& sound);

/// @brief Whose controls @p owner's are in @p song: a synth's sound, a kit's or an audio track's own, a kit row's
/// sound, or the song's; nullopt when it is none of @p song's.
/// @param owner The runtime owner.
/// @param song  The song to look in.
/// @return The controls address, or nullopt.
[[nodiscard]] std::optional<deluge::project::ControlsAddress> controlsAddressOf(::ModControllable& owner, ::Song& song);

/// @brief The runtime owner of @p address's controls in @p song (outputs and hibernating instruments), with whether it
/// is a sound (which has mod knobs), or nullptr.
/// @param song    The song to look in.
/// @param address Whose controls.
/// @param isSound Out: whether the owner is a sound.
/// @return The owner, or nullptr.
[[nodiscard]] ::ModControllableAudio* controlsOwnerAt(::Song& song, deluge::project::ControlsAddress address,
                                                      bool* isSound);

/// @brief Make @p owner's knobs what @p controls say: a sound's mod knobs, and its (or a global effectable's) learned
/// MIDI knobs, in the list's order. A learned knob whose input, relativity and target are unchanged is kept whole: its
/// device and its takeover state with it.
/// @param owner    The owner whose knobs are set.
/// @param isSound  Whether @p owner is a sound (it then also has mod knobs).
/// @param controls The controls to apply.
void applyControls(::ModControllableAudio& owner, bool isSound,
                   const std::vector<deluge::project::ControllerMapping>& controls);

/// @brief Set @p output's own controls record (a synth's sound's knobs, a kit's or an audio track's learned knobs) from
/// its runtime knobs, when it is bound in @p song: after a runtime step that moved knobs between owners (a preset
/// change handing its learned knobs over, an audio track cloned from another).
/// @param output The output.
/// @param song   The song it belongs to.
void recordOutputControls(::Output& output, ::Song& song);

/// @brief The engines the app's model codecs name params through.
/// @return The engines.
[[nodiscard]] deluge::project::CodecPlugins codecPlugins();

} // namespace deluge::bridge
