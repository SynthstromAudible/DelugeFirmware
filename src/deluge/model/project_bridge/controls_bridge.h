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

#include "base/function_ref.h"
#include "instruments/classic/pages.h"
#include "model/fx/effects_owner.h"
#include "model/lane_owner.h"
#include "model/project_bridge/owner_plugin.h"
#include "modulation/knob_target.h"
#include "modulation/params/param.h"
#include "plugin/types/param.h"
#include "project/controller_mapping.h"
#include "project/param_address.h"

#include <array>
#include <optional>
#include <vector>

class Drum;
class Output;
class Sound;
class Song;

namespace deluge::bridge {

/// @brief The param a runtime knob controls, as the model's controls name it: a plain param through its engine's map
/// (@p unpatchedKind numbers an unpatched one), a cable's depth as the instrument's depth id. A knob with no target
/// reads as the null descriptor does.
/// @param knob          The knob's target.
/// @param unpatchedKind The Kind that numbers an unpatched param.
/// @return The target param id.
[[nodiscard]] deluge::plugin::ParamId controlTargetOf(const deluge::modulation::KnobTarget& knob,
                                                      deluge::modulation::params::Kind unpatchedKind);

/// @brief A knob's target as the model's control names it, for an owner of @p owner's plugin: the kind form with the
/// kind that numbers that owner's unpatched params.
/// @param knob  The knob's target.
/// @param owner The owner's plugin.
/// @return The target param id.
[[nodiscard]] deluge::plugin::ParamId controlTargetOf(const deluge::modulation::KnobTarget& knob, OwnerPlugin owner);

/// @brief A sound's controls as the model holds them: its 16 mod knobs (button by button, knob 0 then 1).
/// @param sound The sound.
/// @return The sound's controls.
[[nodiscard]] std::vector<deluge::project::ControllerMapping> controlsOf(::Sound& sound);

/// @brief A sound's controls as the instrument's knob pairs, one per mod button: each knob's target is the param id
/// the record holds (a cable's depth as its depth id). A knob @p controls does not name reads as param id 0
/// (`kOscAVolume`); writeKnobPairs reads a record only when it names every knob.
/// @param controls A sound's controls (as controlsOf gives them).
/// @return The knobs, button by button.
[[nodiscard]] deluge::instruments::classic::KnobPairs
knobPairsOf(const std::vector<deluge::project::ControllerMapping>& controls);

/// @brief A sound's controls holding @p pairs: its 16 mod knobs in controlsOf's order (button by button, knob 0
/// then 1). The inverse of knobPairsOf.
/// @param pairs The knobs, button by button.
/// @return The controls.
[[nodiscard]] std::vector<deluge::project::ControllerMapping>
controlsOf(const deluge::instruments::classic::KnobPairs& pairs);

/// @brief Change @p sound's mod knobs, the one path every knob write takes: @p change maps the knobs (read from the
/// sound's controls record when it resolves in @p song, else from the engine) to the knobs the sound takes. Nothing is
/// written when it changes none. A sound with a record writes through a SetControls Edit, so the record and the
/// engine's knobs take them together; when the Edit refuses, both copies are written directly. A sound with no record
/// (one outside the song) has only its engine knobs to write. The Edit nests inside a running one, as lane publication
/// counts nesting and only the outermost publishes.
/// @param sound  The sound whose knobs change.
/// @param song   The song whose project holds the sound's record.
/// @param change Maps the sound's knobs, one pair per mod button, to the knobs it takes.
/// @return Whether any knob changed.
bool writeKnobPairs(
    ::Sound& sound, ::Song& song,
    deluge::base::function_ref<deluge::instruments::classic::KnobPairs(const deluge::instruments::classic::KnobPairs&)>
        change);

/// @brief Remap @p sound's mod knobs for a change of synth mode from @p from to @p to (the instrument's
/// remap_for_mode), through writeKnobPairs.
/// @param sound The sound whose mode changed.
/// @param song  The song whose project holds the sound's record.
/// @param from  The synth mode before the change.
/// @param to    The synth mode after it.
void remapKnobsForMode(::Sound& sound, ::Song& song, deluge::instruments::classic::SynthMode from,
                       deluge::instruments::classic::SynthMode to);

/// @brief Set every output's, sound kit row's and the song's controls in the song's model from the runtime's mod
/// knobs (a kit, an audio track and the song have none). Run once a song is loaded and after each save, as the slots
/// are filled and refreshed.
/// @param song The song.
void refreshControls(::Song& song);

/// @brief The runtime knob target of a model control's target: the inverse of controlTargetOf (@p unpatchedKind numbers
/// an unpatched param, as there).
/// @param target        The model-side target.
/// @param unpatchedKind The Kind that numbers an unpatched param.
/// @return The knob's target; none if @p target names no runtime param.
[[nodiscard]] deluge::modulation::KnobTarget knobOfControl(deluge::plugin::ParamId target,
                                                           deluge::modulation::params::Kind unpatchedKind);

/// @brief The runtime knob target of a learned param. Only an ENGINE-space param names a runtime knob; any other
/// space has none.
/// @param target        The learned param.
/// @param unpatchedKind The Kind that numbers an unpatched param.
/// @return The knob's target; none if @p target is not an ENGINE param naming a runtime param.
[[nodiscard]] deluge::modulation::KnobTarget knobOfControl(deluge::project::ParamTarget target,
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
[[nodiscard]] std::optional<deluge::project::ControlsAddress> controlsAddressOf(const deluge::model::LaneOwner& owner,
                                                                                ::Song& song);

/// @brief The output of @p song, playing or hibernating, whose id is @p id.
/// @param song The song to look in.
/// @param id   The output's id.
/// @return The output, or nullptr.
[[nodiscard]] ::Output* outputWithId(::Song& song, uint32_t id);

/// @brief The drum of any kit of @p song, playing or hibernating, whose id is @p id.
/// @param song The song to look in.
/// @param id   The drum's id.
/// @return The drum, or nullptr.
[[nodiscard]] ::Drum* drumWithId(::Song& song, uint32_t id);

/// @brief The sound whose controls @p address names in @p song (a synth, or a kit's sound row; outputs and hibernating
/// instruments), which has mod knobs.
/// @param song    The song to look in.
/// @param address Whose controls.
/// @return The sound, or nullptr (the song, a kit, an audio track, another kind of row, or none).
[[nodiscard]] ::Sound* soundAt(::Song& song, deluge::project::ControlsAddress address);

/// @brief The effects of the owner whose controls @p address names in @p song: the song's master, a synth, a kit, an
/// audio track or a sound row (outputs and hibernating instruments).
/// @param song    The song to look in.
/// @param address Whose controls.
/// @return The owner's effects, or nullopt when @p address names none.
[[nodiscard]] std::optional<deluge::model::EffectsOwner> effectsOwnerAt(::Song& song,
                                                                        deluge::project::ControlsAddress address);

/// @brief The owner whose controls @p address names in @p song: the song's master channel, a synth's sound, a kit, an
/// audio track or a kit row's sound.
/// @param song    The song to look in.
/// @param address Whose controls.
/// @return The owner; none when @p address names none.
[[nodiscard]] deluge::model::LaneOwner controlsSlotAt(::Song& song, deluge::project::ControlsAddress address);

/// @brief Make a sound's mod knobs what @p controls say. Its learned MIDI knobs are left as they are.
/// @param sound    The sound whose knobs are set.
/// @param controls The controls to apply.
void applyControls(::Sound& sound, const std::vector<deluge::project::ControllerMapping>& controls);

/// @brief Set @p output's own controls record (a synth's sound's mod knobs) from its runtime mod knobs, when it is
/// bound in @p song.
/// @param output The output.
/// @param song   The song it belongs to.
void recordOutputControls(::Output& output, ::Song& song);

/// @brief The engines the app's model codecs name params through.
/// @return The engines.
[[nodiscard]] deluge::project::CodecPlugins codecPlugins();

} // namespace deluge::bridge
