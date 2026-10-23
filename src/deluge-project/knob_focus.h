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

/// @file
/// @brief What the gold knobs point at, as addresses into the model: the view's choice, never saved.

#include "deluge-project/clip_address.h"
#include "deluge-project/fx_chain.h"
#include "deluge-project/instrument_slot.h"
#include "deluge-project/note_address.h"
#include "deluge-project/param_address.h"
#include "deluge-project/setting_group.h"

#include <cstdint>
#include <optional>
#include <variant>

namespace deluge::project {

/// @brief Which kind of output holds a chain: the model's ChainAddress cannot tell a kit from an audio track.
enum class ChainOwner : uint8_t { KIT, AUDIO_TRACK, SONG };

/// @brief The gold knobs on the song's own chain (the song view with affect-entire, the arranger, performance view).
struct SongFocus {
	bool operator==(const SongFocus&) const = default;
};

/// @brief The gold knobs on a sound: a synth track's, or a kit row's (with its kit, whose mode the row's knobs use).
struct SoundFocus {
	SlotAddress slot;
	std::optional<ClipAddress> clip;   ///< none for an arranger row with no clip
	std::optional<NoteRowAddress> row; ///< a kit row's note row in the clip
	std::optional<uint32_t> kit;       ///< a kit row's kit, by output id
	bool operator==(const SoundFocus&) const = default;
};

/// @brief The gold knobs on a host chain: a kit's with affect-entire, or an audio track's.
struct ChainFocus {
	ChainAddress chain;
	ChainOwner owner{ChainOwner::KIT};
	std::optional<ClipAddress> clip; ///< none for an arranger row with no clip
	bool operator==(const ChainFocus&) const = default;
};

/// @brief The gold knobs on a MIDI track, which send the CCs its knobs name.
struct MidiFocus {
	TrackAddress track;
	std::optional<ClipAddress> clip; ///< none: the knobs have no param to move
	bool operator==(const MidiFocus&) const = default;
};

/// @brief The part of a clip a held pad names: knob turns write a region there.
struct ModRegion {
	uint32_t pos{0xFFFFFFFF}; ///< 0xFFFFFFFF: at the playhead
	uint32_t length{0};       ///< 0: no region is held
	int32_t note_row_id{0};   ///< the held note's row, for a write to that row's expression params
	bool operator==(const ModRegion&) const = default;
};

/// @brief The song's compressor view: whether the knobs edit its settings, and which one the lower knob edits.
struct CompView {
	bool editing{false};
	uint8_t param{0}; ///< ratio, attack, release, sidechain HPF, blend
	bool operator==(const CompView&) const = default;
};

/// @brief What the gold knobs point at: the view's choice, as addresses. View state, never saved.
struct KnobFocus {
	std::variant<std::monostate, SongFocus, SoundFocus, ChainFocus, MidiFocus> owner;
	ModRegion region;
	CompView comp;
	bool vu_meter{false}; ///< the song's VU meter shows on the sidebar in place of the knobs' levels
	bool operator==(const KnobFocus&) const = default;
};

/// @brief The owner @p focus's knobs move params of, as an address with no param: its owner id and clip id.
/// @return None for no focus, or a MIDI track with no clip.
[[nodiscard]] inline std::optional<ParamAddress> knob_owner(const KnobFocus& focus) {
	const auto clip_id = [](const std::optional<ClipAddress>& clip) {
		return clip ? std::optional{clip->clip_id} : std::nullopt;
	};
	if (std::holds_alternative<SongFocus>(focus.owner)) {
		return ParamAddress{.owner = OwnerId::song()};
	}
	if (const auto* sound = std::get_if<SoundFocus>(&focus.owner)) {
		return ParamAddress{.owner = sound->slot.kind == SlotAddress::Kind::DRUM ? OwnerId::drum(sound->slot.id)
		                                                                         : OwnerId::instrument(sound->slot.id),
		                    .clip_id = clip_id(sound->clip)};
	}
	if (const auto* chain = std::get_if<ChainFocus>(&focus.owner)) {
		if (chain->owner == ChainOwner::AUDIO_TRACK) {
			return ParamAddress{.owner = chain->clip ? OwnerId::clip(chain->clip->clip_id)
			                                         : OwnerId::instrument(chain->chain.id)};
		}
		return ParamAddress{.owner = OwnerId::instrument(chain->chain.id), .clip_id = clip_id(chain->clip)};
	}
	if (const auto* midi = std::get_if<MidiFocus>(&focus.owner); midi != nullptr && midi->clip) {
		return ParamAddress{.owner = OwnerId::clip(midi->clip->clip_id)};
	}
	return std::nullopt;
}

} // namespace deluge::project
