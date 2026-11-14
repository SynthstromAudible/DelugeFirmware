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
/// @brief Learned MIDI knobs: a CC or pitch bend a knob of the song's mapping table is learned to moves the knob's
///        param, found by address among the owners that play.

#include "controls/facts.h"
#include "controls/sink.h"
#include "controls/takeover.h"
#include "deluge-project/mapping_table.h"
#include "deluge-project/midi_command.h"
#include "deluge-project/param_address.h"

#include <cstdint>
#include <optional>
#include <span>

namespace deluge::controls {

/// @brief A CC or pitch bend as the learned knobs hear it.
struct LearnedMessage {
	uint8_t channel{0};         ///< raw 0-15: a knob learned on an MPE zone never matches it
	uint8_t number{0};          ///< the CC number; 128 for a pitch bend
	uint16_t value{0};          ///< 7-bit for a CC, 14-bit for a pitch bend
	bool cable_relative{false}; ///< the cable it came on sends relative values
};

/// @brief One learned knob a message moves: a copy of its mapping table entry.
struct LearnedHit {
	project::OwnerParamTarget target; ///< whose param it turns
	project::MidiCommand source;      ///< what it is learned to
	bool relative{false};             ///< it is still taken to send relative values
	[[nodiscard]] bool operator==(const LearnedHit&) const = default;
};

/// @brief The knob hits of one owner for a message, in table order.
using KnobHits = std::span<const LearnedHit>;

/// @brief What a dispatch asks the song's learned knobs: which of an owner's knobs the message moves.
class HitsOf {
public:
	virtual ~HitsOf() = default;
	/// @brief The hits of @p owner for the message, in its table order. They are copies: a write that changes the
	/// table leaves them as they were.
	/// @return The hits, valid until the next call.
	[[nodiscard]] virtual KnobHits of(project::ControlsAddress owner) = 0;
};

/// @brief Where a learned knob's param is, on the owner that plays it.
/// @param target     The knob's target.
/// @param param_owner The owner a ParamAddress of the target's params names: SONG for the song's knobs, the
///                    output's or row's own (PlayingOutput::param_owner, KitRow::param_owner).
/// @param clip_id    The clip a write lands in: an INSTRUMENT or DRUM owner's override, a CLIP owner's own clip
///                   (which replaces the one @p param_owner names, as a take replaces the clip it records).
/// @return The address, or none for a target outside its owner's engine params (a learned knob turns no other) or a
///         CLIP owner with no clip.
[[nodiscard]] std::optional<project::ParamAddress>
resolve_learned(const project::OwnerParamTarget& target, project::OwnerId param_owner, std::optional<uint32_t> clip_id);

/// @brief Move the params of the song's learned knobs a CC is learned to. The song's knobs hear a CC on any channel,
/// an MPE member channel included; it hears no pitch bend.
///
/// For each hit, in order: a value from 16 to 111 on a knob taken to be relative clears its flag; the param is made
/// where missing and read; takeover turns the CC into a position; and a position that moved is written, as a learned
/// write, the UI told.
/// @param hits      The knobs the CC moves.
/// @param facts     The params' values and the takeover inputs.
/// @param sink      Where the writes go.
/// @param takeovers Each knob's takeover state.
/// @param message   The CC.
void dispatch_song_cc(HitsOf& hits, const Facts& facts, Sink& sink, TakeoverStore& takeovers,
                      const LearnedMessage& message);

/// @brief Move the params of one output's learned knobs a CC is learned to: its own, then, for a kit, each of its sound
/// rows' in note-row order. A host offers each output in play order, each before the next output hears the CC
/// itself, as each takes its own take: an output that does not play (playing_outputs()) or is a MIDI or CV track has
/// none.
///
/// For each hit, as dispatch_song_cc, with the step region held on the output's clip read first and then the take the
/// arrangement records it into made, so the param is read and written in the take, over the region.
/// @param output_id The output.
/// @param hits      The knobs the CC moves.
/// @param facts     The playing outputs, a kit's rows, the params' values and the takeover inputs.
/// @param sink      Where the writes go.
/// @param takeovers Each knob's takeover state.
/// @param message   The CC.
void dispatch_output_cc(uint32_t output_id, HitsOf& hits, const Facts& facts, Sink& sink, TakeoverStore& takeovers,
                        const LearnedMessage& message);

/// @brief Move the params of one output's learned knobs a pitch bend is learned to, offered as dispatch_output_cc
/// offers a CC. The bend's 14-bit value is the param's word ((v << 18) - 2^31), with no takeover: of each owner's
/// hits, the first whose param is written takes it.
/// @param output_id The output.
/// @param hits      The knobs the bend moves.
/// @param facts     The playing outputs and a kit's rows.
/// @param sink      Where the writes go.
/// @param message   The bend.
/// @return Whether a learned knob of the output hears the bend, whether or not its param was written: then its
///         instrument does not bend.
[[nodiscard]] bool dispatch_output_bend(uint32_t output_id, HitsOf& hits, const Facts& facts, Sink& sink,
                                        const LearnedMessage& message);

} // namespace deluge::controls
