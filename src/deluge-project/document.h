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

#include "deluge-project/arrangement.h"
#include "deluge-project/arrangement_codec.h"
#include "deluge-project/codec.h"
#include "deluge-project/controller_mapping.h"
#include "deluge-project/doc_audio_clip.h"
#include "deluge-project/doc_clip.h"
#include "deluge-project/doc_drum.h"
#include "deluge-project/doc_output.h"
#include "deluge-project/groove.h"
#include "deluge-project/mapping_table.h"
#include "deluge-project/reverb_settings.h"
#include "deluge-project/scale_settings.h"
#include "deluge-project/transport.h"
#include "serialization/value.h"

#include <cstdint>
#include <expected>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace deluge::project {

/// @brief The model's parts of a whole song, read from or written to a document: the song-level records, the
/// arrangement, and the structure records of every clip, output and drum keyed by id, with the order and kind of the
/// elements that carry them.
struct SongDocument {
	/// A clip's place in the song: its id, and what its element is.
	struct ClipEntry {
		uint32_t id{0};
		bool audio{false};            ///< an `audioClip` (else an `instrumentClip`)
		bool in_kit{false};           ///< its note rows belong to a kit's drums
		bool arrangement_only{false}; ///< in the arrangement's clips rather than the session's
		bool operator==(const ClipEntry&) const = default;
	};
	/// A drum's place in its kit: its id and element.
	struct DrumEntry {
		uint32_t id{0};
		std::string element;
		bool operator==(const DrumEntry&) const = default;
	};
	/// An output's place in the song: its id, its element (`sound`, `kit`, `audioTrack`…) and a kit's drums.
	struct OutputEntry {
		uint32_t id{0};
		std::string element;
		std::vector<DrumEntry> drums;
		bool operator==(const OutputEntry&) const = default;
	};

	Transport transport;
	Groove groove;
	ScaleSettings scale;
	/// Which mod button's pair of params the song's own gold knobs edit (`activeModFunction`); 1 when none is said.
	uint8_t mod_knob_mode{1};
	ReverbSettings reverb;
	Arrangement arrangement;
	std::vector<ClipEntry> clip_order;
	std::vector<OutputEntry> output_order;
	std::map<uint32_t, DocClip> clips;
	std::map<uint32_t, DocAudioClip> audio_clips;
	std::map<uint32_t, DocOutput> outputs;
	std::map<uint32_t, DocDrum> drums;
	/// Every MIDI-learned binding: the sections' launch commands, then the song's knobs, then each output's (its knobs,
	/// its input, and each of a kit's rows' knobs, mute and input) in output order, then each clip's mute in clip
	/// order.
	MappingTable mappings;
	/// The song master's effects.
	FxChain master_effects;
	/// The entry of the clip with @p id, or nullptr.
	[[nodiscard]] const ClipEntry* clip_entry(uint32_t id) const;

	bool operator==(const SongDocument&) const = default;
};

/// @brief Read the model's parts of the song in @p root (an XML- or JSON-parsed document). Elements without an id (a
/// file from before ids) get the next id after the largest of their kind in the document, in document order; a
/// session macro naming its clip or output by position names the one at that position (a session clip, an output in
/// the order of `instruments`), or nothing if there is none.
/// Controls and learned knobs are named through @p engines: a sound's and a sound kit row's by the Deluge instrument, a
/// kit's, an audio track's and the song's by the provisional global-effectable table; with no naming for an engine,
/// its owners have no controls and no learned knobs.
[[nodiscard]] std::expected<SongDocument, LoadError> decode_song(const serialization::Value& root,
                                                                 const CodecEngines& engines);

/// @brief The document `{"song": {…}}` holding @p song's model parts in the JSON shape, recorded as written by
/// @p firmware_version (which tells a reader how the sync values are stored).
[[nodiscard]] serialization::Value encode_song(const SongDocument& song, std::string_view firmware_version,
                                               const CodecEngines& engines);

} // namespace deluge::project
