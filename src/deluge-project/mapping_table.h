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
#include "deluge-project/midi_command.h"
#include "deluge-project/model_memory.h"
#include "engine-api/types/param.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace deluge::project {

/// @brief A param of an owner's controls: the knob turns that param.
struct ParamTarget {
	ControlsAddress owner;
	engine::ParamId param{};
	bool operator==(const ParamTarget&) const = default;
};
/// @brief A clip's mute toggle, by the clip's id.
struct ClipMuteTarget {
	uint32_t clip_id{0};
	bool operator==(const ClipMuteTarget&) const = default;
};
/// @brief A kit row's mute toggle, by the drum's id.
struct DrumMuteTarget {
	uint32_t drum_id{0};
	bool operator==(const DrumMuteTarget&) const = default;
};
/// @brief A section's launch command, by section number.
struct SectionLaunchTarget {
	uint8_t section{0};
	bool operator==(const SectionLaunchTarget&) const = default;
};
/// @brief The MIDI input that plays an output, by the output's id.
struct TrackInputTarget {
	uint32_t output_id{0};
	bool operator==(const TrackInputTarget&) const = default;
};
/// @brief The MIDI input that plays a kit row, by the drum's id.
struct LaneInputTarget {
	uint32_t drum_id{0};
	bool operator==(const LaneInputTarget&) const = default;
};

/// @brief What a learned MIDI command controls.
using MappingTarget =
    std::variant<ParamTarget, ClipMuteTarget, DrumMuteTarget, SectionLaunchTarget, TrackInputTarget, LaneInputTarget>;

/// @brief One learned binding: a source command and what it controls.
struct Mapping {
	MidiCommand source;
	MappingTarget target;
	bool relative{false}; ///< ParamTarget only: the knob sends relative changes
	bool operator==(const Mapping&) const = default;
};

/// @brief Every MIDI-learned binding of a song.
///
/// A param may have several knobs; every other target kind holds at most one source.
class MappingTable {
public:
	/// @brief Learn @p mapping. A ParamTarget replaces the entry whose source and target are both equal to its own,
	/// devices compared exactly, else is appended; its relative flag is set unless the source is a pitch bend
	/// (note_or_cc 128). Any other kind replaces that target's entry whatever its source, else is appended.
	///
	/// A menu's knob learn matches the knob it replaces by how MIDI is heard, which a device reference alone cannot
	/// decide; that caller finds the knob itself and uses relearn().
	/// @param mapping The binding to learn.
	/// @return The entry that now holds it.
	Mapping& learn(Mapping mapping);
	/// @brief Learn @p mapping, a ParamTarget, in place of the entry for the same target whose source is
	/// @p old_source, keeping that entry's place in the table; learn() it when there is no such entry. Its relative
	/// flag is set as learn() sets it.
	/// @param old_source The source of the knob it replaces.
	/// @param mapping    The binding to learn.
	/// @return The entry that now holds it.
	Mapping& relearn(const MidiCommand& old_source, Mapping mapping);
	/// @brief Remove every entry for @p target (for a ParamTarget: every knob on that param of that owner).
	/// @param target What to unlearn.
	/// @return How many entries were removed.
	size_t unlearn(const MappingTarget& target);
	/// @brief The entry @p target has: for a ParamTarget, the first such knob; for any other kind, its only one.
	/// @param target What to look up.
	/// @return The entry, or nullptr.
	[[nodiscard]] const Mapping* find(const MappingTarget& target) const;
	/// @brief Move every entry owned by output @p from (its params, its input) to output @p to. @p to's own such
	/// entries are dropped first.
	/// @param from The output whose entries move.
	/// @param to The output that takes them.
	void retarget_output(uint32_t from, uint32_t to);
	/// @brief Make output @p to's param entries copies of output @p from's: @p to's own are dropped first.
	/// @param from The source output.
	/// @param to The output that gets copies.
	void copy_params(uint32_t from, uint32_t to);
	/// @brief Make clip @p to's mute entry a copy of clip @p from's, or none when @p from has none.
	/// @param from The source clip.
	/// @param to The clip that gets the copy.
	void copy_clip_mute(uint32_t from, uint32_t to);
	/// @brief Make @p owner's knobs @p knobs, in that order: they take the place of the owner's first knob, or go at
	/// the end when it has none. Each is taken as given (its relative flag included); every other entry stays where it
	/// is.
	/// @param owner Whose knobs these are.
	/// @param knobs The owner's knobs; each must be a ParamTarget of @p owner.
	void set_params(ControlsAddress owner, std::span<const Mapping> knobs);
	/// @brief @p owner's knobs, in table order.
	/// @param owner Whose knobs.
	/// @return Copies of its ParamTarget entries.
	[[nodiscard]] std::vector<Mapping> params_of(ControlsAddress owner) const;
	/// @brief Drop every entry that targets clip @p clip_id.
	void forget_clip(uint32_t clip_id);
	/// @brief Drop every entry that targets drum @p drum_id (its mute, its input, its params).
	void forget_drum(uint32_t drum_id);
	/// @brief Drop every entry that targets output @p output_id (its params, its input).
	void forget_output(uint32_t output_id);

	[[nodiscard]] std::span<const Mapping> entries() const { return entries_; }
	std::span<Mapping> entries() { return entries_; }

	bool operator==(const MappingTable&) const = default;

private:
	std::vector<Mapping, ModelAllocator<Mapping>> entries_;
};

} // namespace deluge::project
