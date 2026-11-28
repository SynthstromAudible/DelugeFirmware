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
/// @brief What a control reads that the model does not hold: the engine's live values and runtime state.

#include "plugin/types/param.h"
#include "plugin/types/plugin_id.h"
#include "project/clip_address.h"
#include "project/controller_mapping.h"
#include "project/instrument_slot.h"
#include "project/param_address.h"
#include "ui/controls/takeover.h"

#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace deluge::ui::controls {

/// @brief An output a learned message is offered to: it plays an active clip.
struct PlayingOutput {
	/// What kind of output it is: which of its owners a learned message reaches.
	enum class Kind : uint8_t {
		SYNTH, ///< a sound
		KIT,   ///< a kit: its own knobs, then each of its sound rows'
		AUDIO, ///< an audio track
		OTHER, ///< a MIDI or CV track: nothing is learned onto it
	};
	project::ControlsAddress owner; ///< {OUTPUT, id}: whose knobs a message is offered to
	/// The owner a ParamAddress of its own params names: INSTRUMENT with its id for a synth or a kit, CLIP with the
	/// active clip's id for an audio track.
	project::OwnerId param_owner;
	Kind kind{Kind::OTHER};
	uint32_t clip_id{0}; ///< its active clip
	[[nodiscard]] bool operator==(const PlayingOutput&) const = default;
};

/// @brief A kit's sound row a learned message is offered to.
struct KitRow {
	uint32_t drum_id{0};          ///< the row's drum
	project::OwnerId param_owner; ///< the owner a ParamAddress of the row's own params names: DRUM with its id
	[[nodiscard]] bool operator==(const KitRow&) const = default;
};

/// @brief The facts a gold knob reads that the model does not hold, each answered by the host.
class Facts {
public:
	virtual ~Facts() = default;
	/// @brief The address a knob or a followed CC aimed at @p address moves: the address itself, or, on a sound playing
	/// another engine than the param's, the param that stands for it there (a DX7's level for oscillator A's volume);
	/// nullopt where none does, and the knob moves nothing.
	[[nodiscard]] virtual std::optional<project::ParamAddress>
	playing_address(const project::ParamAddress& address) const {
		return address;
	}
	/// @brief The value the param at @p address plays now: the engine's cursor, which automation moves at the
	/// playhead, a latch or override holds, and stutter-type effects and recording change, where the record holds the
	/// saved base value; nullopt where the song holds no such param.
	[[nodiscard]] virtual std::optional<int32_t> param_value(const project::ParamAddress& address) const = 0;
	/// @brief The value of the lane @p address names itself, never the take the arrangement records its clip into:
	/// current, or as its automation reads at @p pos. MIDI follow's feedback reports the clip it follows, as it is.
	/// @param pos Where in the clip to read; none for the lane's current value.
	/// @return The value, or nullopt where the song holds no such lane.
	[[nodiscard]] virtual std::optional<int32_t> clip_param_value(const project::ParamAddress& address,
	                                                              std::optional<uint32_t> pos) const = 0;
	/// @brief Whether the lane @p address names itself (never its take) holds automation (nodes the playhead follows):
	/// false for a lane the song holds none of.
	[[nodiscard]] virtual bool param_automated(const project::ParamAddress& address) const = 0;
	/// @brief @p address's value at @p pos in its clip, as its automation reads there (the take's while the
	/// arrangement records the clip, in the clip's or the note row's own loop); its current value at 0xFFFFFFFF.
	[[nodiscard]] virtual std::optional<int32_t> param_value_at(const project::ParamAddress& address,
	                                                            uint32_t pos) const = 0;
	/// @brief Whether a stutter is playing: the song's stutter, which a gold knob's push begins, is live state no
	/// record holds.
	[[nodiscard]] virtual bool stutter_active() const = 0;
	/// @brief Whether the sound at @p slot can hold note tails, which a sound drum's kit-arp inclusion needs and a
	/// gold-knob turn repaints for: its params (envelope sustain, noise) decide it, which no setting holds.
	/// @param slot The sound.
	/// @param clip The clip whose params a synth reads (the turn's); none for the clip its output plays. A sound
	///             drum always reads its own row's.
	[[nodiscard]] virtual bool allows_note_tails(project::SlotAddress slot,
	                                             std::optional<project::ClipAddress> clip) const = 0;
	/// @brief The device's alternative delay-knob setting (a runtime feature setting).
	[[nodiscard]] virtual bool alt_delay() const = 0;
	/// @brief The outputs a learned message is offered to, in the song's play order: those with an active clip
	/// and an id (a song member always has one).
	/// The span holds plain values and is valid until the next call to this member: another member's call, and a
	/// sink write (a take cloned), leave it as it was, so a dispatcher may walk it and ask sound_rows() of each
	/// output's clip.
	[[nodiscard]] virtual std::span<const PlayingOutput> playing_outputs() const = 0;
	/// @brief The sound rows of the kit clip @p clip_id, in note-row order: a row whose drum is a sound with an id.
	/// The span is valid until the next call to this member, as playing_outputs()'s is.
	[[nodiscard]] virtual std::span<const KitRow> sound_rows(uint32_t clip_id) const = 0;
	/// @brief What a takeover reads from the app: the global mode, and whether a running clock records. The step
	/// region and the knob's own flag are the caller's to set.
	/// @param cable_relative Whether the cable the message came on sends relative values.
	[[nodiscard]] virtual TakeoverInputs takeover_inputs(bool cable_relative) const = 0;
	/// @brief The step-edit region held on the clip @p clip_id, as {start, length}: none where no region is held on
	/// it, or @p clip_id is none (the song never holds one).
	[[nodiscard]] virtual std::optional<std::pair<int32_t, int32_t>>
	step_region(std::optional<uint32_t> clip_id) const = 0;
	/// @brief The descriptor row of the param @p target names on an owner of the plugin @p plugin: how its stored word
	/// maps onto a knob position. The controls name params by id; the rows live in the owners' plugins, which the
	/// host holds.
	/// @return The row, or null for a cable's depth, an expression dimension, a MIDI CC, or an id the plugin has no
	///         row for.
	[[nodiscard]] virtual const plugin::ParamInfo* param_info(plugin::PluginId plugin,
	                                                          const project::ParamTarget& target) const = 0;
};

} // namespace deluge::ui::controls
