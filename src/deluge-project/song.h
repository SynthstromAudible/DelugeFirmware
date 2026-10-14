/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/arrangement.h"
#include "deluge-project/audio_clip_structure.h"
#include "deluge-project/backed_params.h"
#include "deluge-project/clip_address.h"
#include "deluge-project/clip_params.h"
#include "deluge-project/clip_structure.h"
#include "deluge-project/controller_mapping.h"
#include "deluge-project/drum_structure.h"
#include "deluge-project/groove.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/metadata.h"
#include "deluge-project/output_structure.h"
#include "deluge-project/param_address.h"
#include "deluge-project/param_automation.h"
#include "deluge-project/reverb_settings.h"
#include "deluge-project/scale_settings.h"
#include "deluge-project/transport.h"

namespace deluge::project {

/// @brief The project model: a song's persistent musical state as host data.
///
/// The engine `::Song` holds one as `project`, beside the live engine objects (clips, outputs, the master effect
/// chain) it holds itself (`::Song::live`); access is `currentSong->project.<component>.<field>`.
class Song {
public:
	Song();

	/// @brief Id-addressable DOM param resolution: map a stable `ParamAddress` to the live `DocAutoParam` in the tree
	/// (song-global → `master_params`; clips → the per-clip store by id).
	///
	/// This is the seam that lets param ops resolve by id rather than a threaded ModelStack.
	/// @param address The lane's stable address.
	/// @return The lane, or nullptr when the address names an owner/lane that is not tree-resident, so callers fall
	/// back to the engine resolution path.
	DocAutoParam* resolveParam(const ParamAddress& address);

	/// @brief The slot @p address names (a synth track's or a sound kit row's instrument), or nullptr when its owner
	/// has no record or no slot.
	/// @param address The slot's address.
	/// @return The slot, or nullptr.
	InstrumentSlot* resolveSlot(SlotAddress address);
	/// @brief The controller mappings @p address names: an output's, a kit row's or the song's own, or nullptr for an
	/// output or a drum this song has no record of.
	/// @param address The controls' address.
	/// @return The mappings, or nullptr.
	std::vector<ControllerMapping>* resolveControls(ControlsAddress address);
	/// @brief The chain @p address names: a kit's or an audio track's, or the master's; nullptr for an output this song
	/// has no record of or whose record holds no chain.
	/// @param address The chain's address.
	/// @return The chain, or nullptr.
	[[nodiscard]] FxChain* resolveChain(ChainAddress address);

	/// @brief Find a clip's DOM-owned structural record.
	///
	/// The DocClip is the sole storage for the clip's loop_length/colour/section/launch. Mirrors resolveParam's CLIP
	/// branch.
	/// @param address The clip's address.
	/// @return The record, or nullptr if it is not tree-resident.
	DocClip* resolveClip(const ClipAddress& address);
	/// @brief A clip's structural record, created if absent (bound as the clip's authoritative `docClip_` in the Clip
	/// constructor).
	/// @param address The clip's address.
	/// @return The clip's DocClip.
	DocClip& getOrCreateDocClip(const ClipAddress& address);
	/// @brief Drop a clip's structural record when the clip is destructed, so the store doesn't accumulate dead
	/// entries.
	/// @param address The clip's address.
	void removeDocClip(const ClipAddress& address);

	/// @brief An AudioClip's DOM-owned DocAudioClip, created if absent (bound as the clip's `docAudioClip_` in the
	/// AudioClip constructor). A separate store from DocClip.
	/// @param address The clip's address.
	/// @return The clip's DocAudioClip.
	DocAudioClip& getOrCreateDocAudioClip(const ClipAddress& address);
	/// @brief Drop an AudioClip's descriptor record on destruction.
	/// @param address The clip's address.
	void removeDocAudioClip(const ClipAddress& address);

	Metadata metadata;
	Groove groove;
	Transport transport;
	ScaleSettings scale;
	ReverbSettings reverb;
	Arrangement arrangement;

	/// The song's own effect params (the global-effectable table's ids): the authority for their values and
	/// automation, projected into the engine's master chain.
	DocParamManager master_params;
	/// The song's own learned MIDI knobs, on its effects: refreshed from the runtime at load and at save.
	std::vector<ControllerMapping> controls;
	/// The song master's effects: refreshed from the runtime at load, written by its Edits.
	FxChain master_effects;
	ClipParams clip_params;     // per-clip automation stores, keyed by Clip::id (id-addressable node storage)
	BackedParams backed_params; // instrument-shared automation, keyed by (owner, clip) — persistent form of ParamBackup
	ClipStructure clip_structure;            // per-clip structural records (loop_length…), keyed by Clip::id
	DrumStructure drum_structure;            // per-drum structural records (channel/note…), keyed by Drum::id
	AudioClipStructure audio_clip_structure; // per-AudioClip descriptor records, keyed by Clip::id
	OutputStructure output_structure;        // per-output identity records (name/colour…), keyed by Output::id
};

} // namespace deluge::project
