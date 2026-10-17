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

#include <cstdint>
#include <optional>
#include <vector>

class Drum;
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
[[nodiscard]] deluge::engine::ParamId knobTargetOf(ParamDescriptor descriptor,
                                                   deluge::modulation::params::Kind unpatchedKind);

/// @brief A sound's controls as the model holds them: its 16 mod knobs (button by button, knob 0 then 1).
/// @param sound The sound.
/// @return The sound's controls.
[[nodiscard]] std::vector<deluge::project::ControllerMapping> controlsOf(::Sound& sound);

/// @brief Set every output's, sound kit row's and the song's controls in the song's model from the runtime's mod
/// knobs (a kit, an audio track and the song have none). Run once a song is loaded and after each save, as the slots
/// are filled and refreshed.
/// @param song The song.
void refreshControls(::Song& song);

/// @brief The runtime descriptor of a knob's target: the inverse of knobTargetOf (@p unpatchedKind numbers an unpatched
/// param, as there).
/// @param target        The model-side target.
/// @param unpatchedKind The Kind that numbers an unpatched param.
/// @return The runtime descriptor; null if @p target names no runtime param.
[[nodiscard]] ParamDescriptor descriptorOf(deluge::engine::ParamId target,
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

/// @brief The runtime owner of @p address's controls in @p song (outputs and hibernating instruments), with whether it
/// is a sound (which has mod knobs), or nullptr.
/// @param song    The song to look in.
/// @param address Whose controls.
/// @param isSound Out: whether the owner is a sound.
/// @return The owner, or nullptr.
[[nodiscard]] ::ModControllableAudio* controlsOwnerAt(::Song& song, deluge::project::ControlsAddress address,
                                                      bool* isSound);

/// @brief [task] Which outputs and kit rows a song holds, by id, as outputWithId() and drumWithId() find them, taken
/// once as a save of the song begins. The save asks, for each owner it writes, whether the song holds it: while one is
/// open for the song, holdsOutput(), holdsDrum() and savedKnobsOf() (mapping_bridge.h) answer from its sorted lists in
/// a binary search, where outputWithId() and drumWithId() walk every output and kit row. An id it does not hold (one
/// the save gave an owner since) is left to those walks. Its pointers are only compared, never followed: the save frees
/// a kit row no clip plays.
class HeldOwners {
public:
	/// @param song The song being saved.
	explicit HeldOwners(::Song& song);
	~HeldOwners();
	HeldOwners(const HeldOwners&) = delete;
	HeldOwners& operator=(const HeldOwners&) = delete;
	HeldOwners(HeldOwners&&) = delete;
	HeldOwners& operator=(HeldOwners&&) = delete;

	/// @brief The innermost one open for @p song.
	/// @param song The song.
	/// @return It, or nullptr when none is open for @p song.
	[[nodiscard]] static const HeldOwners* openFor(const ::Song& song);

	/// @brief Whether @p output is the output outputWithId() found at its id.
	/// @param output An output with an id.
	/// @return Whether it is, or nullopt when no output had that id.
	[[nodiscard]] std::optional<bool> holds(const ::Output& output) const;

	/// @brief Whether @p drum is the kit row drumWithId() found at its id.
	/// @param drum A kit row with an id.
	/// @return Whether it is, or nullopt when no kit row had that id.
	[[nodiscard]] std::optional<bool> holds(const ::Drum& drum) const;

	/// @brief The owner of an output's or a kit row's controls at @p address, as controlsOwnerAt() found it.
	/// @param address An output's or a kit row's controls.
	/// @return The owner (nullptr when it has none: a MIDI or CV track, a row that is not a sound), or nullopt when no
	///         output or kit row had that id, or @p address is the song's.
	[[nodiscard]] std::optional<const ::ModControllableAudio*>
	controlsAt(deluge::project::ControlsAddress address) const;

private:
	/// An owner at its id: the first of that id in the walk's order.
	struct Owner {
		uint32_t id;
		const void* object;
		const ::ModControllableAudio* controls;
	};

	[[nodiscard]] static const Owner* find(const std::vector<Owner>& owners, uint32_t id);

	const ::Song& song_;
	std::vector<Owner> outputs_;
	std::vector<Owner> drums_;
	const HeldOwners* outer_;
};

/// @brief Make a sound's mod knobs what @p controls say. Its learned MIDI knobs are left as they are.
/// @param owner    The owner whose knobs are set.
/// @param isSound  Whether @p owner is a sound (only a sound has mod knobs).
/// @param controls The controls to apply.
void applyControls(::ModControllableAudio& owner, bool isSound,
                   const std::vector<deluge::project::ControllerMapping>& controls);

/// @brief Set @p output's own controls record (a synth's sound's mod knobs) from its runtime mod knobs, when it is
/// bound in @p song.
/// @param output The output.
/// @param song   The song it belongs to.
void recordOutputControls(::Output& output, ::Song& song);

/// @brief The engines the app's model codecs name params through.
/// @return The engines.
[[nodiscard]] deluge::project::CodecEngines codecEngines();

} // namespace deluge::bridge
