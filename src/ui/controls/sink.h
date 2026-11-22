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
/// @brief Where a control's writes go: the requests a host applies as Edits or runtime commands, so the controls name
///        no Edit and no engine object.

#include "project/controller_mapping.h"
#include "project/fx_chain.h"
#include "project/instrument_slot.h"
#include "project/mapping_table.h"
#include "project/midi_command.h"
#include "project/param_address.h"
#include "project/setting_group.h"
#include "serialization/value.h"
#include "ui/controls/knob_focus.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace deluge::ui::controls {

/// @brief Whose effects a write names: a sound's, or a host chain's (a kit's, an audio track's, the song's).
using EffectOwner = std::variant<project::SlotAddress, project::ChainAddress>;

/// @brief How a param write finds its param, and what it leaves marked.
enum class Resolution : uint8_t {
	KNOB,    ///< as a gold knob writes it: what it turns is made, the take written while the arrangement records, and
	         ///< the instrument marked edited
	LEARNED, ///< as a learned or followed MIDI knob writes it: the same, with no owner marked edited
};

/// @brief Takes the writes a gold knob makes. The app's sink applies each as the target's Edit or runtime command; a
/// spec's records them.
class Sink {
public:
	virtual ~Sink() = default;
	/// @brief Write the param at @p address over a region, as a gold knob or a learned or followed MIDI knob writes it
	/// (@p resolution says which); no MIDI-follow feedback (the caller sends its own).
	/// @param address The param's address.
	/// @param value   The new value, as the engine stores it.
	/// @param pos     The region's start; with @p length 0, the value is set at the playhead.
	/// @param length  The region's length, or 0.
	/// @param resolution How the param is found, and whether the instrument is marked edited. Always named, so no
	///                   caller leaves it to a default a derived sink would not see.
	/// @return Whether it was taken.
	virtual bool write_param(const project::ParamAddress& address, int32_t value, int32_t pos, int32_t length,
	                         Resolution resolution) = 0;
	/// @brief The app's UI follows a learned write: the automation editor's grid and the performance view's display
	/// show the param that moved.
	/// @param address  The param written, as the write landed (a take's clip id, where there is one).
	/// @param knob_pos The knob position the write moved the param to, -64..64.
	virtual void learned_written(const project::ParamAddress& address, int32_t knob_pos) = 0;
	/// @brief Make the take the arrangement records @p output_id's active clip into, once; later calls find it.
	/// @param output_id The output's id.
	/// @return The id of the clip writes to the output now go to (the active clip itself where no take is made), or
	///         nullopt where the song holds no such output or it has no active clip.
	virtual std::optional<uint32_t> take_for(uint32_t output_id) = 0;
	/// @brief Make the param at @p address where the song does not hold it yet, as a learned knob does before it reads
	/// the param, whether or not it then moves it: a cable's depth is added at 0, in an undo step of its own when no
	/// write follows. Nothing is written.
	/// @return Whether the song holds the param now.
	virtual bool make_param(const project::ParamAddress& address) = 0;
	/// @brief A knob learned as relative was sent an absolute value (16..111): clear its relative flag in the table.
	/// @param target The knob's target.
	/// @param source The command the knob is learned to.
	virtual void knob_absolute(const project::OwnerParamTarget& target, const project::MidiCommand& source) = 0;
	/// @brief Set @p owner's gold-knob mode to @p mode.
	/// @return Whether it was taken: false for a mode past the last button or a track the song does not hold.
	virtual bool knob_mode(project::KnobModeOwner owner, uint8_t mode) = 0;
	/// @brief Set the view word @p word of @p chain's view key @p key (`currentFilterType`, `modFXCurrentParam`): which
	/// value its knobs edit.
	/// @return Whether it was taken: false for a chain, key or word that does not resolve.
	virtual bool chain_view(project::ChainAddress chain, std::string_view key, std::string_view word) = 0;
	/// @brief Make @p controls the knob mappings of @p owner, as a push that cycles a knob's param leaves them.
	/// @return Whether it was taken.
	virtual bool write_controls(project::ControlsAddress owner, std::vector<project::ControllerMapping> controls) = 0;
	/// @brief Begin (or end) the stutter on @p focus's owner: a runtime command, nothing saved.
	/// @return Whether it was taken: false where @p focus names no owner with a chain.
	virtual bool stutter(const KnobFocus& focus, bool begin) = 0;
	/// @brief Set @p focus's params to their automated values at @p pos in its clip, as a held pad grabs them: a
	/// runtime command, nothing saved.
	/// @return Whether it was taken: false where @p focus names no owner.
	virtual bool grab_region(const KnobFocus& focus, uint32_t pos) = 0;
	/// @brief Write effect setting @p path of @p owner as the word @p value: a gesture's exact value, which a menu
	/// value may not name.
	/// @return Whether it was taken: false for an owner, path or value the effects refuse.
	virtual bool write_effect(const EffectOwner& owner, std::string_view path, const serialization::Value& value) = 0;
	/// @brief Enter (or leave, @p on false) choosing knob @p knob's CC on @p focus's MIDI track with the select
	/// encoder: a runtime command on the clip minder's state.
	/// @return Whether it was taken.
	virtual bool midi_cc_select(const KnobFocus& focus, int32_t knob, bool on) = 0;
	/// @brief Delete the automation of the param at @p address as a gold knob's shift+push does: while the arrangement
	/// records the clip, in its take.
	/// @return Whether it was taken.
	virtual bool delete_knob_automation(const project::ParamAddress& address) = 0;
	/// @brief Mark the instrument @p focus's knobs edit as edited by the user (a synth, a kit row's kit, a kit's own
	/// chain): a runtime flag no record holds, which a gesture that edited it sets, and the menu after a select turn on
	/// an item that edits a sound's instrument.
	/// @return Whether it was taken.
	virtual bool instrument_edited(const KnobFocus& focus) = 0;
};

} // namespace deluge::ui::controls
