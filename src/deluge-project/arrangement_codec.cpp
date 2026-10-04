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

#include "deluge-project/arrangement_codec.h"

#include "serialization/tree.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace deluge::project {

using serialization::int_of;
using serialization::text_of;
using serialization::Value;

namespace {

using Port = MidiDeviceRef::Port;

/// The text a hex attribute holds: `0x` and @p digits upper-case digits.
Value hex_text(uint32_t value, int digits) {
	char text[11];
	std::snprintf(text, sizeof text, "0x%0*X", digits, value);
	return Value::makeScalar(text);
}

Value encode_device(const MidiDeviceRef& device) {
	auto map = Value::makeMap();
	switch (device.port) {
	case Port::DIN:
		map.set("port", Value::makeScalar("din"));
		break;
	case Port::UPSTREAM_USB_1:
		map.set("port", Value::makeScalar("upstreamUSB"));
		break;
	case Port::UPSTREAM_USB_2:
		map.set("port", Value::makeScalar("upstreamUSB2"));
		break;
	case Port::UPSTREAM_USB_3:
		map.set("port", Value::makeScalar("upstreamUSB3"));
		break;
	case Port::HOSTED:
		map.set("name", Value::makeScalar(device.name));
		map.set("vendorId", hex_text(device.vendor_id, 4));
		map.set("productId", hex_text(device.product_id, 4));
		break;
	}
	return map;
}

/// A device reference as the device manager reads one: a known port, else a hosted device if the reference names one
/// or gives a vendor id, else none.
std::optional<MidiDeviceRef> decode_device(const Value& node) {
	if (!node.isMap()) {
		return std::nullopt;
	}
	if (const std::string* port = text_of(node.find("port"))) {
		if (*port == "upstreamUSB") {
			return MidiDeviceRef{.port = Port::UPSTREAM_USB_1};
		}
		if (*port == "upstreamUSB2") {
			return MidiDeviceRef{.port = Port::UPSTREAM_USB_2};
		}
		if (*port == "upstreamUSB3") {
			return MidiDeviceRef{.port = Port::UPSTREAM_USB_3};
		}
		if (*port == "din") {
			return MidiDeviceRef{.port = Port::DIN};
		}
	}
	MidiDeviceRef hosted{.port = Port::HOSTED};
	if (const std::string* name = text_of(node.find("name"))) {
		hosted.name = *name;
	}
	hosted.vendor_id = static_cast<uint16_t>(serialization::hex_of(node.find("vendorId")).value_or(0));
	hosted.product_id = static_cast<uint16_t>(serialization::hex_of(node.find("productId")).value_or(0));
	if (!hosted.name.empty() || hosted.vendor_id != 0) {
		return hosted;
	}
	return std::nullopt;
}

std::string_view kind_text(SessionMacroKind kind) {
	switch (kind) {
	case SessionMacroKind::CLIP_LAUNCH:
		return "clip_launch";
	case SessionMacroKind::OUTPUT_CYCLE:
		return "output_cycle";
	case SessionMacroKind::SECTION:
		return "section";
	case SessionMacroKind::NO_MACRO:
	case SessionMacroKind::NUM_KINDS:
		break;
	}
	return {};
}

} // namespace

Value encode_sections(const Arrangement& arrangement) {
	auto list = Value::makeArray();
	for (int32_t s = 0; s < kMaxNumSections; s++) {
		const Section& section = arrangement.sections[s];
		auto map = Value::makeMap();
		map.set("id", Value::makeNumber(s));
		map.set("numRepeats", Value::makeNumber(section.num_repetitions));
		const MidiCommand& command = section.launch_midi_command;
		if (command.contains_something()) {
			map.set("midiCommandChannel", Value::makeNumber(command.channel_or_zone));
			map.set("midiCommandNote", Value::makeNumber(command.note_or_cc));
			if (command.device) {
				map.set("midiCommandDevice", encode_device(*command.device));
			}
		}
		auto box = Value::makeMap();
		box.set("section", std::move(map));
		list.append(std::move(box));
	}
	return list;
}

Value encode_macros(const Arrangement& arrangement) {
	size_t count = 0;
	for (size_t y = 0; y < arrangement.session_macros.size(); y++) {
		if (arrangement.session_macros[y].kind != SessionMacroKind::NO_MACRO) {
			count = y + 1;
		}
	}
	auto list = Value::makeArray();
	for (size_t y = 0; y < count; y++) {
		const SessionMacro& m = arrangement.session_macros[y];
		auto map = Value::makeMap();
		switch (m.kind) {
		case SessionMacroKind::CLIP_LAUNCH:
			if (m.clip_id) {
				map.set("kind", Value::makeScalar(std::string(kind_text(m.kind))));
				map.set("clipId", Value::makeNumber(static_cast<int32_t>(*m.clip_id)));
			}
			break;
		case SessionMacroKind::OUTPUT_CYCLE:
			if (m.output_id) {
				map.set("kind", Value::makeScalar(std::string(kind_text(m.kind))));
				map.set("outputId", Value::makeNumber(static_cast<int32_t>(*m.output_id)));
			}
			break;
		case SessionMacroKind::SECTION:
			map.set("kind", Value::makeScalar(std::string(kind_text(m.kind))));
			map.set("section", Value::makeNumber(m.section));
			break;
		case SessionMacroKind::NO_MACRO:
		case SessionMacroKind::NUM_KINDS:
			break;
		}
		auto box = Value::makeMap();
		box.set("macro", std::move(map));
		list.append(std::move(box));
	}
	return list;
}

std::expected<void, LoadError> decode(const Value& song, Arrangement& arrangement, LegacyMacroTargets* legacy) {
	if (!song.isMap()) {
		return std::unexpected(LoadError::NOT_A_MAP);
	}
	if (const Value* sections = song.find("sections")) {
		for (const Value* node : serialization::elements_named(*sections, "section")) {
			if (!node->isMap()) {
				continue;
			}
			const int32_t id = static_cast<uint8_t>(int_of(node->find("id")).value_or(255));
			int16_t repeats = static_cast<int16_t>(int_of(node->find("numRepeats")).value_or(0));
			if (repeats < -2 || repeats > 9999) {
				repeats = 0;
			}
			const auto channel = static_cast<uint8_t>(int_of(node->find("midiCommandChannel")).value_or(255));
			const auto note = static_cast<uint8_t>(int_of(node->find("midiCommandNote")).value_or(255));
			if (id >= kMaxNumSections) {
				continue;
			}
			Section& section = arrangement.sections[id];
			if (channel < MidiCommand::kFirstProgramChange && note < 128) {
				const Value* device = node->find("midiCommandDevice");
				section.launch_midi_command = MidiCommand{
				    .channel_or_zone = channel,
				    .note_or_cc = note,
				    .device = device != nullptr ? decode_device(*device) : std::nullopt,
				};
			}
			section.num_repetitions = repeats;
		}
	}
	if (const Value* macros = song.find("sessionMacros")) {
		size_t y = 0;
		for (const Value* node : serialization::elements_named(*macros, "macro")) {
			if (y >= arrangement.session_macros.size()) {
				break;
			}
			const size_t slot = y++;
			if (!node->isMap()) {
				continue;
			}
			SessionMacro& m = arrangement.session_macros[slot];
			if (const std::string* kind = text_of(node->find("kind"))) {
				if (*kind == "clip_launch") {
					m.kind = SessionMacroKind::CLIP_LAUNCH;
				}
				else if (*kind == "output_cycle") {
					m.kind = SessionMacroKind::OUTPUT_CYCLE;
				}
				else if (*kind == "section") {
					m.kind = SessionMacroKind::SECTION;
				}
			}
			if (auto id = int_of(node->find("clipId"))) {
				m.clip_id = static_cast<uint32_t>(*id);
			}
			if (auto id = int_of(node->find("outputId"))) {
				m.output_id = static_cast<uint32_t>(*id);
			}
			if (auto section = int_of(node->find("section"))) {
				m.section = static_cast<uint8_t>(*section);
			}
			const std::optional<int32_t> clipIndex = int_of(node->find("clip"));
			const std::optional<int32_t> outputIndex = int_of(node->find("output"));
			if (legacy != nullptr) {
				legacy->clip_index[slot] = clipIndex;
				legacy->output_index[slot] = outputIndex;
			}
			const bool hasClip = m.clip_id.has_value() || (legacy != nullptr && clipIndex.has_value());
			const bool hasOutput = m.output_id.has_value() || (legacy != nullptr && outputIndex.has_value());
			if ((m.kind == SessionMacroKind::CLIP_LAUNCH && !hasClip)
			    || (m.kind == SessionMacroKind::OUTPUT_CYCLE && !hasOutput)) {
				m.kind = SessionMacroKind::NO_MACRO;
			}
		}
	}
	return {};
}

} // namespace deluge::project
