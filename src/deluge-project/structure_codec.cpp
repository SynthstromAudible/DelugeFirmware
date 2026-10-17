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

#include "deluge-project/structure_codec.h"

#include "deluge-project/arp_codec.h"
#include "deluge-project/audio_input.h"
#include "engine-api/types/setting.h"

#include "deluge-project/arrangement.h"
#include "deluge-project/launch_style.h"
#include "deluge-project/sequence_direction.h"
#include "engine-api/types/engine_id.h"
#include "serialization/tree.h"

#include <algorithm>
#include <array>
#include <string>

namespace deluge::project {

using serialization::int_of;
using serialization::text_of;
using serialization::Value;

namespace {

Value text(std::string_view s) {
	return Value::makeScalar(std::string(s));
}

bool is_midi_element(std::string_view element) {
	return element == "midi" || element == "midiChannel" || element == "mpeZone";
}

} // namespace

Value encode(const DocClip& clip) {
	auto map = Value::makeMap();
	if (!clip.name.empty()) {
		map.set("clipName", text(clip.name));
	}
	if (clip.sequence_direction != SequenceDirection::FORWARD) {
		map.set("sequenceDirection", text(to_string(clip.sequence_direction)));
	}
	for (std::string_view path : {"midiBank", "midiSub", "midiPGM", "bendRange", "bendRangeMPE"}) {
		if (const std::optional<int32_t>* value = value_at(clip.values, path); value->has_value()) {
			map.set(std::string{path}, Value::makeNumber(**value));
		}
	}
	if (clip.values.arp) {
		map.set("arpeggiator", encode(*clip.values.arp, std::nullopt));
	}
	map.set("length", Value::makeNumber(clip.loop_length));
	map.set("colourOffset", Value::makeNumber(clip.colour_offset));
	if (clip.section != 255) {
		map.set("section", Value::makeNumber(clip.section));
	}
	if (clip.launch_style != LaunchStyle::DEFAULT) {
		map.set("launchStyle", text(to_string(clip.launch_style)));
	}
	return map;
}

std::expected<void, LoadError> decode(const Value& clipElement, DocClip& clip) {
	if (!clipElement.isMap()) {
		return std::unexpected(LoadError::NOT_A_MAP);
	}
	if (auto section = int_of(clipElement.find("section"))) {
		clip.section = std::min(static_cast<uint8_t>(*section), static_cast<uint8_t>(kMaxNumSections - 1));
	}
	if (auto colour = int_of(clipElement.find("colourOffset"))) {
		clip.colour_offset = static_cast<int16_t>(*colour);
	}
	if (const std::string* name = text_of(clipElement.find("clipName"))) {
		clip.name = *name;
	}
	if (const std::string* direction = text_of(clipElement.find("sequenceDirection"))) {
		clip.sequence_direction = parse_sequence_direction(*direction).value_or(SequenceDirection::FORWARD);
	}
	for (std::string_view path : {"midiBank", "midiSub", "midiPGM", "bendRange", "bendRangeMPE"}) {
		if (auto value = int_of(clipElement.find(path))) {
			*value_at(clip.values, path) = *value;
		}
	}
	if (const Value* arp = clipElement.find("arpeggiator")) {
		decode(*arp, clip.values.arp.emplace(), std::nullopt);
	}
	if (const std::string* style = text_of(clipElement.find("launchStyle"))) {
		clip.launch_style = parse_launch_style(*style).value_or(LaunchStyle::DEFAULT);
	}
	const Value* length = clipElement.find("length");
	if (length == nullptr) {
		length = clipElement.find("trackLength");
	}
	if (auto value = int_of(length)) {
		clip.loop_length = std::max<int32_t>(1, *value);
	}
	return {};
}

Value encode(const DocNoteRow& row, bool in_kit) {
	auto map = Value::makeMap();
	if (!in_kit) {
		map.set("y", Value::makeNumber(row.y));
	}
	if (row.muted) {
		map.set("muted", Value::makeNumber(1));
	}
	if (in_kit) {
		map.set("colourOffset", Value::makeNumber(row.colour_offset));
	}
	if (row.loop_length_if_independent != 0) {
		map.set("length", Value::makeNumber(row.loop_length_if_independent));
	}
	if (row.sequence_direction != SequenceDirection::OBEY_PARENT) {
		map.set("sequenceDirection", text(to_string(row.sequence_direction)));
	}
	if (row.bend_range) {
		map.set("bendRange", Value::makeNumber(*row.bend_range));
	}
	return map;
}

std::expected<void, LoadError> decode(const Value& rowElement, DocNoteRow& row, int32_t& min_y) {
	if (!rowElement.isMap()) {
		return std::unexpected(LoadError::NOT_A_MAP);
	}
	if (auto length = int_of(rowElement.find("length"))) {
		row.loop_length_if_independent = *length;
	}
	if (auto muted = int_of(rowElement.find("muted"))) {
		row.muted = *muted != 0;
	}
	if (auto y = int_of(rowElement.find("y"))) {
		row.y = static_cast<int16_t>(*y);
	}
	if (auto colour = int_of(rowElement.find("colourOffset"))) {
		row.colour_offset = static_cast<int8_t>(*colour);
	}
	if (const std::string* direction = text_of(rowElement.find("sequenceDirection"))) {
		row.sequence_direction = parse_sequence_direction(*direction).value_or(SequenceDirection::FORWARD);
	}
	if (auto bend = int_of(rowElement.find("bendRange"))) {
		row.bend_range = *bend;
	}
	row.y = static_cast<int16_t>(std::max<int32_t>(row.y, static_cast<int16_t>(min_y)));
	min_y = row.y + 1;
	return {};
}

Value encode(const DocAudioClip& clip) {
	auto map = Value::makeMap();
	map.set("filePath", text(clip.file_path));
	map.set("startSamplePos", Value::makeNumber(static_cast<int32_t>(clip.start_pos)));
	map.set("endSamplePos", Value::makeNumber(static_cast<int32_t>(clip.end_pos)));
	map.set("pitchSpeedIndependent", Value::makeNumber(clip.pitch_and_speed_independent ? 1 : 0));
	if (clip.interpolation_mode == engine::InterpolationMode::LINEAR) {
		map.set("linearInterpolation", Value::makeNumber(1));
	}
	if (clip.reversed) {
		map.set("reversed", Value::makeNumber(1));
	}
	map.set("attack", Value::makeNumber(clip.attack));
	map.set("priority", Value::makeNumber(static_cast<int32_t>(clip.voice_priority)));
	if (clip.transpose != 0) {
		map.set("transpose", Value::makeNumber(clip.transpose));
	}
	if (clip.cents != 0) {
		map.set("cents", Value::makeNumber(clip.cents));
	}
	map.set("overdubsShouldCloneAudioTrack", Value::makeNumber(clip.overdubs_should_clone ? 1 : 0));
	return map;
}

std::expected<void, LoadError> decode(const Value& clipElement, DocAudioClip& clip) {
	if (!clipElement.isMap()) {
		return std::unexpected(LoadError::NOT_A_MAP);
	}
	if (const std::string* path = text_of(clipElement.find("filePath"))) {
		clip.file_path = *path;
	}
	if (auto v = int_of(clipElement.find("overdubsShouldCloneAudioTrack"))) {
		clip.overdubs_should_clone = *v != 0;
	}
	if (auto v = int_of(clipElement.find("startSamplePos"))) {
		clip.start_pos = static_cast<uint64_t>(static_cast<int64_t>(*v));
	}
	if (auto v = int_of(clipElement.find("endSamplePos"))) {
		clip.end_pos = static_cast<uint64_t>(static_cast<int64_t>(*v));
	}
	if (auto v = int_of(clipElement.find("pitchSpeedIndependent"))) {
		clip.pitch_and_speed_independent = *v != 0;
	}
	if (auto v = int_of(clipElement.find("linearInterpolation")); v && *v != 0) {
		clip.interpolation_mode = engine::InterpolationMode::LINEAR;
	}
	if (auto v = int_of(clipElement.find("attack"))) {
		clip.attack = *v;
	}
	if (auto v = int_of(clipElement.find("priority"))) {
		clip.voice_priority = static_cast<engine::VoicePriority>(*v);
	}
	if (auto v = int_of(clipElement.find("reversed"))) {
		clip.reversed = *v != 0;
	}
	if (auto v = int_of(clipElement.find("transpose"))) {
		clip.transpose = static_cast<int16_t>(*v);
	}
	if (auto v = int_of(clipElement.find("cents"))) {
		clip.cents = static_cast<int8_t>(*v);
	}
	return {};
}

DocOutput new_output(std::string_view element) {
	DocOutput output;
	if (is_midi_element(element)) {
		output.output_mpe_y = kMidiOutputMpeYDefault;
	}
	return output;
}

/// The CC numbers a MIDI instrument can label: 0..119.
constexpr int64_t kNumCcLabels = 120;

Value encode_identity(const DocOutput& output, std::string_view element) {
	auto map = Value::makeMap();
	if (output.muted_in_arrangement) {
		map.set("isMutedInArrangement", Value::makeNumber(1));
	}
	if (output.soloing_in_arrangement) {
		map.set("isSoloingInArrangement", Value::makeNumber(1));
	}
	map.set("isArmedForRecording", Value::makeNumber(output.armed_for_recording ? 1 : 0));
	map.set("activeModFunction", Value::makeNumber(output.mod_knob_mode));
	map.set("colour", Value::makeNumber(output.colour));
	if (element == "audioTrack") {
		map.set("name", text(output.name));
		map.set("mode", text(kAudioModeWords[output.audio_mode]));
		map.set("inputChannel", text(kInputChannelWords[output.input_channel]));
		return map;
	}
	if (!output.name.empty()) {
		map.set("presetName", text(output.name));
	}
	if (!output.dir_path.empty()) {
		map.set("presetFolder", text(output.dir_path));
	}
	map.set("defaultVelocity", Value::makeNumber(output.default_velocity));
	if (is_midi_element(element)) {
		if (output.channel == kMidiChannelLowerZone || output.channel == kMidiChannelUpperZone) {
			map.set("zone", text(output.channel == kMidiChannelLowerZone ? "lower" : "upper"));
		}
		else if (output.channel == kMidiChannelTranspose) {
			map.set("internalDest", text("transpose"));
		}
		else {
			map.set("channel", Value::makeNumber(output.channel));
		}
		map.set("suffix", Value::makeNumber(output.channel_suffix));
		auto polyToMono = Value::makeMap();
		polyToMono.set("aftertouch", Value::makeNumber(output.collapse_aftertouch ? 1 : 0));
		polyToMono.set("mpe", Value::makeNumber(output.collapse_mpe ? 1 : 0));
		polyToMono.set("yCC", Value::makeNumber(output.output_mpe_y));
		map.set("polyToMonoConversion", std::move(polyToMono));
		auto definition = Value::makeMap();
		definition.set("name", text(output.device_definition_file_name));
		auto device = Value::makeMap();
		device.set("definitionFile", std::move(definition));
		auto labels = Value::makeMap();
		for (const auto& [cc, label] : output.cc_labels) {
			labels.set(std::to_string(cc), text(label));
		}
		device.set("ccLabels", std::move(labels));
		auto hide = Value::makeMap();
		hide.set("value", Value::makeNumber(output.hide_unlabeled_cc ? 1 : 0));
		device.set("hideUnlabeledCC", std::move(hide));
		map.set("midiDevice", std::move(device));
	}
	else if (element == "cvChannel") {
		map.set("channel", Value::makeNumber(output.channel));
		map.set("cv2Source", Value::makeNumber(static_cast<int32_t>(output.cv2_source)));
	}
	return map;
}

std::expected<void, LoadError> decode_identity(const Value& outputElement, std::string_view element,
                                               DocOutput& output) {
	if (!outputElement.isMap()) {
		return std::unexpected(LoadError::NOT_A_MAP);
	}
	const Value& o = outputElement;
	if (auto v = int_of(o.find("isMutedInArrangement"))) {
		output.muted_in_arrangement = *v != 0;
	}
	if (auto v = int_of(o.find("isSoloingInArrangement"))) {
		output.soloing_in_arrangement = *v != 0;
	}
	if (auto v = int_of(o.find("isArmedForRecording"))) {
		output.armed_for_recording = *v != 0;
	}
	if (auto v = int_of(o.find("activeModFunction"))) {
		output.mod_knob_mode = static_cast<uint8_t>(*v);
	}
	if (auto v = int_of(o.find("colour"))) {
		output.colour = static_cast<int16_t>(*v);
	}
	if (element == "audioTrack") {
		if (const std::string* name = text_of(o.find("name"))) {
			output.name = *name;
		}
		// A file from before the modes says a monitored track is echoing: today's looper.
		if (auto echoing = int_of(o.find("echoingInput")); echoing && *echoing != 0) {
			output.audio_mode = 2;
		}
		if (const std::string* mode = text_of(o.find("mode"))) {
			output.audio_mode = static_cast<uint8_t>(index_of_word(kAudioModeWords, *mode).value_or(0));
		}
		if (const std::string* channel = text_of(o.find("inputChannel"))) {
			output.input_channel = static_cast<int8_t>(index_of_word(kInputChannelWords, *channel).value_or(0));
		}
		return {};
	}
	if (const std::string* name = text_of(o.find("presetName"))) {
		output.name = *name;
	}
	if (const std::string* folder = text_of(o.find("presetFolder"))) {
		output.dir_path = *folder;
	}
	if (auto v = int_of(o.find("defaultVelocity"))) {
		const auto velocity = static_cast<uint8_t>(*v);
		if (velocity != 0 && velocity < 128) {
			output.default_velocity = velocity;
		}
	}
	if (is_midi_element(element)) {
		if (auto v = int_of(o.find("channel"))) {
			output.channel = *v;
		}
		if (auto v = int_of(o.find("midiChannel"))) {
			output.channel = *v;
		}
		if (const std::string* zone = text_of(o.find("zone"))) {
			if (*zone == "lower") {
				output.channel = kMidiChannelLowerZone;
			}
			else if (*zone == "upper") {
				output.channel = kMidiChannelUpperZone;
			}
		}
		if (const std::string* dest = text_of(o.find("internalDest")); dest != nullptr && *dest == "transpose") {
			output.channel = kMidiChannelTranspose;
		}
		if (auto v = int_of(o.find("suffix"))) {
			output.channel_suffix = *v;
		}
		if (const Value* polyToMono = o.find("polyToMonoConversion"); polyToMono != nullptr && polyToMono->isMap()) {
			if (auto v = int_of(polyToMono->find("aftertouch"))) {
				output.collapse_aftertouch = *v != 0;
			}
			if (auto v = int_of(polyToMono->find("mpe"))) {
				output.collapse_mpe = *v != 0;
			}
			if (auto v = int_of(polyToMono->find("yCC"))) {
				output.output_mpe_y = static_cast<uint8_t>(*v);
			}
		}
		if (const Value* device = o.find("midiDevice"); device != nullptr && device->isMap()) {
			if (const Value* definition = device->find("definitionFile");
			    definition != nullptr && definition->isMap()) {
				if (const std::string* name = text_of(definition->find("name"))) {
					output.device_definition_file_name = *name;
				}
			}
			if (const Value* labels = device->find("ccLabels"); labels != nullptr && labels->isMap()) {
				for (const auto& [cc, label] : labels->entries()) {
					const std::optional<int64_t> number = engine::whole_number(cc);
					const std::string* name = text_of(&label);
					if (number && *number >= 0 && *number < kNumCcLabels && name != nullptr && !name->empty()) {
						output.cc_labels[static_cast<uint8_t>(*number)] = *name;
					}
				}
			}
			if (const Value* hide = device->find("hideUnlabeledCC"); hide != nullptr) {
				if (auto v = int_of(hide->isMap() ? hide->find("value") : hide)) {
					output.hide_unlabeled_cc = *v != 0;
				}
			}
		}
	}
	else if (element == "cvChannel") {
		if (auto v = int_of(o.find("channel"))) {
			output.channel = *v;
		}
		if (auto v = int_of(o.find("cv2Source"))) {
			output.cv2_source = static_cast<CVMode>(*v);
		}
	}
	return {};
}

namespace {

/// The keys of a synth track's element the model reads (decode_song's id, decode_identity's for a `sound`, its
/// controls, and the knobs and MIDI input the mapping table holds).
constexpr std::array<std::string_view, 14> kSoundOutputKeys{
    "id",        "isMutedInArrangement", "isSoloingInArrangement", "isArmedForRecording", "activeModFunction",
    "colour",    "presetName",           "presetFolder",           "defaultVelocity",     "modKnobs",
    "midiKnobs", "inputMidiChannel",     "inputMPEZone",           "inputMidiDevice"};
/// The keys of a sound kit row's element the model reads (decode_song's id, decode_identity's name, its controls, its
/// arp, which its lane record holds, and the knobs, mute command and input the mapping table holds).
constexpr std::array<std::string_view, 7> kSoundDrumKeys{
    "id", "name", "modKnobs", "midiKnobs", "arpeggiator", "midiMuteCommand", "midiInput"};

template <size_t N>
InstrumentSlot slot_without(const Value& element, const std::array<std::string_view, N>& ownKeys) {
	InstrumentSlot slot{.engine = std::string(deluge::engine::kDelugeInstrument.name)};
	if (!element.isMap()) {
		return slot;
	}
	for (const auto& [key, value] : element.entries()) {
		if (std::ranges::find(ownKeys, key) == ownKeys.end()) {
			slot.state.set(key, value);
		}
	}
	return slot;
}

} // namespace

InstrumentSlot slot_of_output(const Value& element, int32_t tick_magnitude) {
	InstrumentSlot slot = slot_without(element, kSoundOutputKeys);
	slot.tick_magnitude = tick_magnitude;
	return slot;
}

InstrumentSlot slot_of_drum(const Value& element, int32_t tick_magnitude) {
	InstrumentSlot slot = slot_without(element, kSoundDrumKeys);
	slot.tick_magnitude = tick_magnitude;
	return slot;
}

std::string_view drum_element(DrumType type) {
	switch (type) {
	case DrumType::MIDI:
		return "midiOutput";
	case DrumType::GATE:
		return "gateOutput";
	case DrumType::SOUND:
		break;
	}
	return "sound";
}

Value encode_identity(const DocDrum& drum) {
	auto map = Value::makeMap();
	map.set("name", text(drum.name));
	if (drum.drum_type != DrumType::SOUND) {
		map.set("channel", Value::makeNumber(drum.channel));
	}
	if (drum.drum_type == DrumType::MIDI) {
		map.set("note", Value::makeNumber(drum.note));
	}
	return map;
}

std::expected<void, LoadError> decode_identity(const Value& drumElement, std::string_view element, DocDrum& drum) {
	if (!drumElement.isMap()) {
		return std::unexpected(LoadError::NOT_A_MAP);
	}
	drum.drum_type = element == "midiOutput"   ? DrumType::MIDI
	                 : element == "gateOutput" ? DrumType::GATE
	                                           : DrumType::SOUND;
	if (const std::string* name = text_of(drumElement.find("name"))) {
		drum.name = *name;
	}
	if (drum.drum_type != DrumType::SOUND) {
		if (auto v = int_of(drumElement.find("channel"))) {
			drum.channel = static_cast<uint8_t>(*v);
		}
	}
	if (drum.drum_type == DrumType::MIDI) {
		if (auto v = int_of(drumElement.find("note"))) {
			drum.note = static_cast<uint8_t>(*v);
		}
	}
	return {};
}

} // namespace deluge::project
