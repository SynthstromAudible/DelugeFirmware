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

#include "deluge-project/mapping_codec.h"

#include "deluge-project/knob_ref.h"
#include "deluge-project/midi_command_codec.h"
#include "serialization/tree.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace deluge::project {

using serialization::Value;

namespace {

using detail::ref_in;
using detail::set_ref;
using detail::text;

/// The channel values naming the lower and upper MPE zones.
constexpr uint8_t kLowerZone = 16;
constexpr uint8_t kUpperZone = 17;
/// The highest note a note command holds.
constexpr int32_t kHighestNote = 127;

uint8_t byte_of(const Value* node) {
	return static_cast<uint8_t>(serialization::int_of(node).value_or(0));
}

/// @p channel moved to the zone @p zone names, or left as it is for a zone the runtime does not know.
uint8_t with_zone(uint8_t channel, const Value* zone) {
	if (zone == nullptr) {
		return channel;
	}
	if (text(zone) == "lower") {
		return kLowerZone;
	}
	if (text(zone) == "upper") {
		return kUpperZone;
	}
	return channel;
}

/// The zone a channel of 16 or more writes as: the lower zone's, else the upper's.
Value zone_text(uint8_t channel) {
	return Value::makeScalar(channel == kLowerZone ? "lower" : "upper");
}

/// A note command element (`channel` or `mpeZone`, `note`, `device`), or nullopt when it names no channel.
std::optional<MidiCommand> note_command(const Value* node) {
	if (node == nullptr || !node->isMap()) {
		return std::nullopt;
	}
	MidiCommand command;
	if (auto channel = serialization::int_of(node->find("channel"))) {
		command.channel_or_zone = static_cast<uint8_t>(*channel);
	}
	command.channel_or_zone = with_zone(command.channel_or_zone, node->find("mpeZone"));
	if (auto note = serialization::int_of(node->find("note"))) {
		command.note_or_cc = static_cast<uint8_t>(std::min(*note, kHighestNote));
	}
	if (const Value* device = node->find("device")) {
		command.device = decode_device(*device);
	}
	if (!command.contains_something()) {
		return std::nullopt;
	}
	return command;
}

/// @p command as a note command element: a channel of 16 or more as a zone.
Value note_element(const MidiCommand& command) {
	auto map = Value::makeMap();
	if (command.channel_or_zone >= kLowerZone) {
		map.set("mpeZone", zone_text(command.channel_or_zone));
	}
	else {
		map.set("channel", Value::makeNumber(command.channel_or_zone));
	}
	map.set("note", Value::makeNumber(command.note_or_cc));
	if (command.device) {
		map.set("device", encode_device(*command.device));
	}
	return map;
}

void decode_note(const Value& element, std::string_view key, MappingTarget target, MappingTable& table) {
	if (std::optional<MidiCommand> command = note_command(element.find(key))) {
		table.learn(Mapping{.source = std::move(*command), .target = target});
	}
}

void encode_note(const MappingTable& table, const MappingTarget& target, std::string_view key, Value& element) {
	if (const Mapping* entry = table.find(target)) {
		element.set(std::string(key), note_element(entry->source));
	}
}

void decode_knobs(const Value& element, ControlsAddress owner, MappingTable& table,
                  const deluge::engine::ParamNaming* naming) {
	std::vector<Mapping> knobs;
	if (const Value* midiKnobs = element.find("midiKnobs"); midiKnobs != nullptr && naming != nullptr) {
		for (const Value* knob : serialization::elements_named(*midiKnobs, "midiKnob")) {
			if (!knob->isMap()) {
				continue;
			}
			const std::optional<deluge::engine::ParamId> param = naming->id_of(ref_in(*knob));
			if (!param) {
				continue;
			}
			Mapping& learned = knobs.emplace_back();
			learned.target = ParamTarget{owner, *param};
			learned.source.channel_or_zone = with_zone(byte_of(knob->find("channel")), knob->find("mpeZone"));
			learned.source.note_or_cc = byte_of(knob->find("ccNumber"));
			learned.relative = serialization::int_of(knob->find("relative")).value_or(0) != 0;
			if (const Value* device = knob->find("device")) {
				learned.source.device = decode_device(*device);
			}
		}
	}
	table.set_params(owner, knobs);
}

void encode_knobs(const MappingTable& table, ControlsAddress owner, Value& element,
                  const deluge::engine::ParamNaming* naming) {
	if (naming == nullptr) {
		return;
	}
	Value midiKnobs = Value::makeArray();
	for (const Mapping& entry : table.params_of(owner)) {
		Value knob = Value::makeMap();
		const uint8_t channel = entry.source.channel_or_zone;
		if (channel == kLowerZone || channel == kUpperZone) {
			knob.set("mpeZone", zone_text(channel));
		}
		else {
			knob.set("channel", Value::makeNumber(channel));
		}
		knob.set("ccNumber", Value::makeNumber(entry.source.note_or_cc));
		knob.set("relative", Value::makeNumber(entry.relative ? 1 : 0));
		set_ref(knob, naming->ref_of(std::get<ParamTarget>(entry.target).param));
		if (entry.source.device) {
			knob.set("device", encode_device(*entry.source.device));
		}
		Value box = Value::makeMap();
		box.set("midiKnob", std::move(knob));
		midiKnobs.append(std::move(box));
	}
	if (!midiKnobs.items().empty()) {
		element.set("midiKnobs", std::move(midiKnobs));
	}
}

void decode_track_input(const Value& element, uint32_t output_id, MappingTable& table) {
	MidiCommand command;
	if (auto channel = serialization::int_of(element.find("inputMidiChannel"))) {
		command.channel_or_zone = static_cast<uint8_t>(*channel);
	}
	command.channel_or_zone = with_zone(command.channel_or_zone, element.find("inputMPEZone"));
	if (const Value* device = element.find("inputMidiDevice")) {
		command.device = decode_device(*device);
	}
	if (command.contains_something()) {
		table.learn(Mapping{.source = std::move(command), .target = TrackInputTarget{output_id}});
	}
}

void encode_track_input(const MappingTable& table, uint32_t output_id, Value& element) {
	const Mapping* entry = table.find(TrackInputTarget{output_id});
	if (entry == nullptr) {
		return;
	}
	const uint8_t channel = entry->source.channel_or_zone;
	if (channel >= kLowerZone) {
		element.set("inputMPEZone", zone_text(channel));
	}
	else {
		element.set("inputMidiChannel", Value::makeNumber(channel));
	}
	if (entry->source.device) {
		element.set("inputMidiDevice", encode_device(*entry->source.device));
	}
}

} // namespace

void decode_mappings(const Value& element, MappingOwner owner, MappingTable& table,
                     const deluge::engine::ParamNaming* naming) {
	if (!element.isMap()) {
		return;
	}
	if (const auto* controls = std::get_if<ControlsAddress>(&owner)) {
		decode_knobs(element, *controls, table, naming);
	}
	else if (const auto* clip = std::get_if<ClipOwner>(&owner)) {
		decode_note(element, "muteMidiCommand", ClipMuteTarget{clip->clip_id}, table);
	}
	else if (const auto* drum = std::get_if<DrumOwner>(&owner)) {
		decode_note(element, "midiMuteCommand", DrumMuteTarget{drum->drum_id}, table);
		decode_note(element, "midiInput", LaneInputTarget{drum->drum_id}, table);
	}
	else if (const auto* track = std::get_if<TrackOwner>(&owner)) {
		if (track->kit) {
			decode_note(element, "MIDIInput", TrackInputTarget{track->output_id}, table);
		}
		else {
			decode_track_input(element, track->output_id, table);
		}
	}
}

void encode_mappings(const MappingTable& table, MappingOwner owner, Value& element,
                     const deluge::engine::ParamNaming* naming) {
	if (const auto* controls = std::get_if<ControlsAddress>(&owner)) {
		encode_knobs(table, *controls, element, naming);
	}
	else if (const auto* clip = std::get_if<ClipOwner>(&owner)) {
		encode_note(table, ClipMuteTarget{clip->clip_id}, "muteMidiCommand", element);
	}
	else if (const auto* drum = std::get_if<DrumOwner>(&owner)) {
		encode_note(table, LaneInputTarget{drum->drum_id}, "midiInput", element);
		encode_note(table, DrumMuteTarget{drum->drum_id}, "midiMuteCommand", element);
	}
	else if (const auto* track = std::get_if<TrackOwner>(&owner)) {
		if (track->kit) {
			encode_note(table, TrackInputTarget{track->output_id}, "MIDIInput", element);
		}
		else {
			encode_track_input(table, track->output_id, element);
		}
	}
}

void decode_section_command(const Value& node, uint8_t section, MappingTable& table) {
	const auto channel = static_cast<uint8_t>(serialization::int_of(node.find("midiCommandChannel")).value_or(255));
	const auto note = static_cast<uint8_t>(serialization::int_of(node.find("midiCommandNote")).value_or(255));
	if (channel >= MidiCommand::kFirstProgramChange || note > kHighestNote) {
		return;
	}
	const Value* device = node.find("midiCommandDevice");
	table.learn(Mapping{.source = MidiCommand{.channel_or_zone = channel,
	                                          .note_or_cc = note,
	                                          .device = device != nullptr ? decode_device(*device) : std::nullopt},
	                    .target = SectionLaunchTarget{section}});
}

void encode_section_command(const MappingTable& table, uint8_t section, Value& node) {
	const Mapping* entry = table.find(SectionLaunchTarget{section});
	if (entry == nullptr || !entry->source.contains_something()) {
		return;
	}
	node.set("midiCommandChannel", Value::makeNumber(entry->source.channel_or_zone));
	node.set("midiCommandNote", Value::makeNumber(entry->source.note_or_cc));
	if (entry->source.device) {
		node.set("midiCommandDevice", encode_device(*entry->source.device));
	}
}

} // namespace deluge::project
