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
#include "deluge-project/drum_structure.h"
#include "deluge-project/groove.h"
#include "deluge-project/metadata.h"
#include "deluge-project/output_structure.h"
#include "deluge-project/param_address.h"
#include "deluge-project/param_automation.h"
#include "deluge-project/reverb_settings.h"
#include "deluge-project/scale_settings.h"
#include "deluge-project/transport.h"

namespace deluge::project {

/// The project model: a song's persistent musical state as host data. The engine `::Song` holds one as `project`,
/// beside the live engine objects (clips, outputs, the master effect chain) it still holds itself (`::Song::live`);
/// access is `currentSong->project.<component>.<field>`.
class Song {
public:
	Song();

	/// Id-addressable DOM param resolution: map a stable `ParamAddress` to the live `DocAutoParam` in the tree
	/// (song-global → `master_params`; clips → the per-clip store by id, added in a later slice). Returns
	/// nullptr when the address names an owner/lane not (yet) tree-resident, so callers fall back to the engine
	/// resolution path. This is the seam that lets param ops resolve by id rather than a threaded ModelStack.
	DocAutoParam* resolveParam(const ParamAddress& address);

	/// M3 owner-1 (clip structure): resolve a clip's DOM-owned structural record (find; nullptr if not tree-resident),
	/// or getOrCreate it (bound as the clip's authoritative `docClip_` in the Clip ctor). Mirrors resolveParam's CLIP
	/// branch. The DocClip is the sole storage for the clip's loop_length/colour/section/launch — no engine shadow.
	DocClip* resolveClip(const ClipAddress& address);
	DocClip& getOrCreateDocClip(const ClipAddress& address);
	/// Drop a clip's structural record when the clip is destructed, so the store doesn't accumulate dead entries.
	void removeDocClip(const ClipAddress& address);

	/// M3 owner-4 (audio-clip descriptors): getOrCreate an AudioClip's DOM-owned DocAudioClip (bound as the clip's
	/// `docAudioClip_` in the AudioClip ctor), or drop it on destruction. Separate store from DocClip. Mirrors
	/// getOrCreateDocClip/removeDocClip.
	DocAudioClip& getOrCreateDocAudioClip(const ClipAddress& address);
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
	ClipParams clip_params;     // per-clip automation stores, keyed by Clip::id (id-addressable node storage)
	BackedParams backed_params; // instrument-shared automation, keyed by (owner, clip) — persistent form of ParamBackup
	ClipStructure clip_structure; // M3 owner-1: per-clip structural records (loop_length…), keyed by Clip::id
	DrumStructure drum_structure; // M3 owner-3: per-drum structural records (channel/note…), keyed by Drum::id
	AudioClipStructure audio_clip_structure; // M3 owner-4: per-AudioClip descriptor records, keyed by Clip::id
	OutputStructure output_structure; // M3 owner-5: per-output identity records (name/colour…), keyed by Output::id
};

} // namespace deluge::project
