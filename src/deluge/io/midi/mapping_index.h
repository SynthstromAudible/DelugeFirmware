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

#include "definitions_cxx.hpp"
#include "deluge-project/controller_mapping.h"
#include "deluge-project/mapping_table.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <tuple>
#include <vector>

class Song;

namespace deluge::midi {

/// @brief A binding of the song's mapping table that incoming MIDI can trigger, its device resolved to a cable.
struct IndexedEntry {
	const project::Mapping* mapping{};
	/// The cable the binding listens to when inputs are told apart by device: nullptr for any device, and
	/// MappingIndex::absentDevice() for a device the Deluge has no cable for, which no message arrives on.
	MIDICable* cable{};
	/// Its place: a section's number, a clip's index in the session, a knob's place in its owner's list of learned
	/// knobs (table order, as the owner's knob list holds them), a row's place in its kit.
	uint32_t ordinal{0};
};

/// @brief The song's MIDI bindings, ready for incoming MIDI to find what it triggers without scanning every owner.
///
/// Each source names its channel or zone and note or CC exactly as LearnedMIDI matches them. A device is resolved once,
/// at rebuild; whether inputs are told apart by device, and a cable's MPE zones, are read when a message is matched.
/// The entries point into the table, so the index is rebuilt whenever the table changes.
class MappingIndex {
private:
	/// A section's or a session clip's command. Sorted by channel, then rank: sections by number, then clips by session
	/// index descending.
	struct Command {
		int32_t channel;
		uint32_t rank;
		IndexedEntry entry;
	};
	/// A learned knob. Sorted by channel, owner, then its place in the owner's list.
	struct Knob {
		uint8_t channel;
		project::ControlsAddress owner;
		IndexedEntry entry;
	};
	/// A kit row's input or mute. Sorted by kit, channel, the row's place, then input before mute.
	struct Lane {
		uint32_t kit;
		int32_t channel;
		bool mute;
		IndexedEntry entry;
	};

public:
	/// @brief The cable an entry names when its device is one the Deluge has no cable for. Never dereferenced.
	[[nodiscard]] static MIDICable* absentDevice();

	/// @brief Index @p table's bindings of @p song. A source's device is resolved to its cable. A binding whose owner
	/// hibernates or is not in @p song is left out, as is a clip's that is not in the session, a track's input (which
	/// its instrument matches for itself) and an entry with no source.
	/// @param table The song's mapping table.
	/// @param song  The song it belongs to.
	void rebuild(const project::MappingTable& table, ::Song& song);

	/// @brief Forget every entry.
	void clear();

	/// @brief Call @p fn with each section's launch command (by section, ascending) and then each session clip's mute
	/// command (by session index, descending) that MIDI from @p from on @p channel with @p note triggers: the same
	/// channel or zone (a CC's or a program change's encoded, as the dispatch offers them) and note, as
	/// LearnedMIDI::equalsNoteOrCC matches them.
	/// @param from    The cable the MIDI came from.
	/// @param channel The channel as the dispatch offers it.
	/// @param note    The note, CC or program.
	/// @param fn      Called with each IndexedEntry hit, in that order.
	template <class Fn>
	void forEachCommand(MIDICable& from, int32_t channel, int32_t note, Fn&& fn) const {
		const auto [first, last] = std::ranges::equal_range(commands_, channel, {}, &Command::channel);
		for (auto it = first; it != last; ++it) {
			if (it->entry.mapping->source.note_or_cc == note && fromDevice(it->entry, from)) {
				fn(it->entry);
			}
		}
	}

	/// @brief Call @p fn with each of @p owner's learned knobs, in its list's order, that a CC (or, as 128, a pitch
	/// bend) @p cc from @p from on @p channel moves, as LearnedMIDI::equalsNoteOrCC matches it.
	/// @param owner   Whose knobs.
	/// @param from    The cable the MIDI came from.
	/// @param channel The incoming channel.
	/// @param cc      The CC number, or 128 for pitch bend.
	/// @param fn      Called with each IndexedEntry hit, in that order.
	template <class Fn>
	void forEachKnob(const project::ControlsAddress& owner, MIDICable& from, int32_t channel, int32_t cc,
	                 Fn&& fn) const {
		const auto key = std::tuple{channel, owner.kind, owner.id};
		const auto [first, last] = std::ranges::equal_range(
		    knobs_, key, {}, [](const Knob& k) { return std::tuple{int32_t{k.channel}, k.owner.kind, k.owner.id}; });
		for (auto it = first; it != last; ++it) {
			if (it->entry.mapping->source.note_or_cc == cc && fromDevice(it->entry, from)) {
				fn(it->entry);
			}
		}
	}

	/// @brief Call @p fn, row by row of kit @p kit_id in its order, with the row's input when a note @p note from
	/// @p from on @p channel sounds it, else with its mute command when that note matches it. A row's input matches as
	/// LearnedMIDI::equalsNoteOrCCAllowMPE does: @p channel taken to its MPE zone on @p from's input port, and only
	/// from a named device. Its mute matches as LearnedMIDI::equalsNoteOrCC does.
	/// @param kit_id  The kit's output id.
	/// @param from    The cable the MIDI came from.
	/// @param channel The incoming channel.
	/// @param note    The incoming note.
	/// @param fn      Called with each IndexedEntry hit (a LaneInputTarget's or a DrumMuteTarget's), in that order.
	template <class Fn>
	void forEachLaneMatch(uint32_t kit_id, MIDICable& from, int32_t channel, int32_t note, Fn&& fn) const {
		const int32_t zone = from.ports[MIDI_DIRECTION_INPUT_TO_DELUGE].channelToZone(channel);
		const auto [inFirst, inLast] = laneRange(kit_id, zone);
		const auto [muteFirst, muteLast] = laneRange(kit_id, channel);
		auto nextInput = [&](auto it) {
			// An input learned from any device sounds no row: LearnedMIDI::equalsChannelAllowMPE refuses a command with
			// no cable.
			while (it != inLast
			       && (it->mute || it->entry.cable == nullptr || it->entry.mapping->source.note_or_cc != note
			           || !fromDevice(it->entry, from))) {
				++it;
			}
			return it;
		};
		auto nextMute = [&](auto it) {
			while (it != muteLast
			       && (!it->mute || it->entry.mapping->source.note_or_cc != note || !fromDevice(it->entry, from))) {
				++it;
			}
			return it;
		};
		auto in = nextInput(inFirst);
		auto mute = nextMute(muteFirst);
		while (in != inLast || mute != muteLast) {
			if (mute == muteLast || (in != inLast && in->entry.ordinal <= mute->entry.ordinal)) {
				// A row its input sounds is not muted by the same note.
				if (mute != muteLast && mute->entry.ordinal == in->entry.ordinal) {
					mute = nextMute(++mute);
				}
				fn(in->entry);
				in = nextInput(++in);
			}
			else {
				fn(mute->entry);
				mute = nextMute(++mute);
			}
		}
	}

private:
	/// @brief Whether a binding listening to @p entry's device hears MIDI from @p from, as LearnedMIDI::equalsCable has
	/// it.
	static bool fromDevice(const IndexedEntry& entry, const MIDICable& from) {
		return !MIDIDeviceManager::differentiatingInputsByDevice || entry.cable == nullptr || entry.cable == &from;
	}

	[[nodiscard]] std::ranges::subrange<std::vector<Lane>::const_iterator> laneRange(uint32_t kit,
	                                                                                 int32_t channel) const {
		return std::ranges::equal_range(lanes_, std::tuple{kit, channel}, {},
		                                [](const Lane& l) { return std::tuple{l.kit, l.channel}; });
	}

	std::vector<Command> commands_;
	std::vector<Knob> knobs_;
	std::vector<Lane> lanes_;
};

/// @brief The index of the current song's bindings.
MappingIndex& mappingIndex();

/// @brief Bring the index into line with @p song's mapping table, when @p song is the current song.
/// @param song The song whose table changed.
void rebuildMappingIndex(::Song& song);

} // namespace deluge::midi
