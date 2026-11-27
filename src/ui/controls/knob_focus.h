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

#include "project/clip_address.h"
#include "project/fx_chain.h"
#include "project/instrument_slot.h"
#include "project/kit_row_address.h"
#include "project/note_address.h"
#include "project/owner_plugin.h"
#include "project/param_address.h"
#include "project/setting_group.h"
#include "ui/controls/board.h"

#include <cstdint>
#include <optional>
#include <variant>

namespace deluge::project {
class Song;
} // namespace deluge::project

namespace deluge::ui::controls {

/// @brief Which kind of output holds a chain: the model's ChainAddress cannot tell a kit from an audio track.
enum class ChainOwner : uint8_t { KIT, AUDIO_TRACK, SONG };

/// @brief The gold knobs on the song's own chain (the song view with affect-entire, the arranger, performance view).
struct SongFocus {
	bool operator==(const SongFocus&) const = default;
};

/// @brief The gold knobs on a sound: a synth track's, or a kit row's (with its kit, whose mode the row's knobs use).
struct SoundFocus {
	project::SlotAddress slot;
	std::optional<project::ClipAddress> clip;   ///< none for an arranger row with no clip
	std::optional<project::NoteRowAddress> row; ///< a kit row's note row in the clip
	std::optional<uint32_t> kit;                ///< a kit row's kit, by output id
	bool operator==(const SoundFocus&) const = default;
};

/// @brief The gold knobs on a host chain: a kit's with affect-entire, or an audio track's.
struct ChainFocus {
	project::ChainAddress chain;
	ChainOwner owner{ChainOwner::KIT};
	std::optional<project::ClipAddress> clip; ///< none for an arranger row with no clip
	bool operator==(const ChainFocus&) const = default;
};

/// @brief The gold knobs on a MIDI track, which send the CCs its knobs name.
struct MidiFocus {
	project::TrackAddress track;
	std::optional<project::ClipAddress> clip; ///< none: the knobs have no param to move
	bool operator==(const MidiFocus&) const = default;
};

/// @brief An owner with no knobs: its knobs turn nothing and its mod buttons choose nothing.
struct NoControlsFocus {
	enum class Kind : uint8_t { CV_TRACK, MIDI_DRUM, GATE_DRUM } kind{Kind::CV_TRACK};
	project::TrackAddress track;                ///< the CV track, or the drum's kit
	std::optional<project::KitRowAddress> drum; ///< a drum's kit and durable drum id; none for a CV track. Only
	                                            ///< operator== reads it: two drums of one kit are different focuses
	std::optional<uint32_t> clip_id;            ///< the clip the view is on, if any
	[[nodiscard]] constexpr bool operator==(const NoControlsFocus&) const = default;
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
	std::variant<std::monostate, SongFocus, SoundFocus, ChainFocus, MidiFocus, NoControlsFocus> owner;
	ModRegion region;
	CompView comp;
	bool vu_meter{false}; ///< the song's VU meter shows on the sidebar in place of the knobs' levels
	bool operator==(const KnobFocus&) const = default;
};

/// @brief Whether @p focus has no knobs to turn: no focus, or an owner with an empty layout.
[[nodiscard]] constexpr bool has_no_knobs(const KnobFocus& focus) {
	return std::holds_alternative<std::monostate>(focus.owner) || std::holds_alternative<NoControlsFocus>(focus.owner);
}

/// @brief Whose table the ENGINE ids @p focus's knobs move are in: a sound's the instrument's, a chain's or the song's
///        the host chain's. A MIDI track's knobs move CCs and expression axes, in no table.
[[nodiscard]] constexpr project::OwnerPlugin owner_plugin_of(const KnobFocus& focus) {
	return std::holds_alternative<SoundFocus>(focus.owner) ? project::OwnerPlugin::INSTRUMENT
	                                                       : project::OwnerPlugin::GLOBAL_EFFECTABLE;
}

/// @brief The owner @p focus's knobs move params of, as an address with no param: its owner id and clip id.
/// @return None for no focus, an owner with no controls, or a MIDI track with no clip.
[[nodiscard]] std::optional<project::ParamAddress> knob_owner(const KnobFocus& focus);

/// @brief Whose gold-knob mode @p focus's knobs follow: the song's, or a track's (a kit row's kit's).
/// @return None for no focus, an owner with no controls (a CV track has no mode), or a drum that names no kit: the
///         owner knob_mode reads, so the two agree.
[[nodiscard]] std::optional<project::KnobModeOwner> knob_mode_owner(const KnobFocus& focus);

/// @brief The section the gold knobs of @p focus follow, from @p song's records: the song's, an output's, a kit row's
///        kit's. Past @p board's last section, the last.
/// @param song  The song, whose records hold the modes.
/// @param focus What the knobs point at.
/// @param board The board the knobs are on; it must have a section (none panics).
/// @return The mode; 0 for no focus or an owner with no controls.
[[nodiscard]] uint8_t knob_mode(const project::Song& song, const KnobFocus& focus, const Board& board);

} // namespace deluge::ui::controls
