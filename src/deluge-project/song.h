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
#include "deluge-project/backed_params.h"
#include "deluge-project/clip_address.h"
#include "deluge-project/clip_params.h"
#include "deluge-project/clip_structure.h"
#include "deluge-project/clips.h"
#include "deluge-project/global_effects.h"
#include "deluge-project/groove.h"
#include "deluge-project/media_pool.h"
#include "deluge-project/metadata.h"
#include "deluge-project/param_address.h"
#include "deluge-project/param_backup.h"
#include "deluge-project/reverb_settings.h"
#include "deluge-project/scale_settings.h"
#include "deluge-project/transport.h"

namespace deluge::project {

/// The project model: the song's persistent musical state, extracted out of the legacy `::Song`
/// god-object (docs/dev/project_model_extraction.md). `::Song` remains the controller and owns one
/// of these as `project`; access is `currentSong->project.<component>.<field>`.
///
/// REPRESENTATION (important): this is still the *runtime* model, not yet a pure document model. Its
/// components fall into two tiers, and the boundary between them is the seam a future authoritative
/// document model ("Level 3", docs/dev/project_model_extraction.md) would split out:
///
///   - **Document / value tier** — value-typed, serializable description, no engine dependency.
///     These are already document-shaped (this is what the idiomatic-refinement pass produced).
///
///   - **Runtime-reference tier** — live, polymorphic, pool-allocated engine objects and the
///     intrusive links the audio thread walks every render block (`media.first_output->next`);
///     `clips` is an ordered list of non-owning `Clip*`; `global_fx`/`param_backup` hold live DSP
///     state. None of these is serializable *as data* — they are the running engine.
///
/// The split is not perfectly clean: some value-tier data still embeds raw runtime pointers
/// (`SessionMacro::clip`/`output`), which a document model would replace with stable IDs. That
/// embedded-pointer case is exactly what the Level-3 projection probe should measure first.
class Song {
public:
	Song();

	/// Id-addressable DOM param resolution: map a stable `ParamAddress` to the live `DocAutoParam` in the tree
	/// (song-global → `global_fx.params`; clips → the per-clip store by id, added in a later slice). Returns
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

	// Document / value tier — value-typed, serializable.
	Metadata metadata;
	Groove groove;
	Transport transport;
	ScaleSettings scale;
	ReverbSettings reverb;
	Arrangement arrangement;

	// Runtime-reference tier — live engine objects, intrusive links, hot-path DSP state.
	MediaPool media;
	Clips clips;
	GlobalEffects global_fx;
	ClipParams clip_params;     // per-clip automation stores, keyed by Clip::id (id-addressable node storage)
	BackedParams backed_params; // instrument-shared automation, keyed by (owner, clip) — persistent form of ParamBackup
	ParamBackup param_backup;
	ClipStructure clip_structure; // M3 owner-1: per-clip structural records (loop_length…), keyed by Clip::id
};

} // namespace deluge::project
