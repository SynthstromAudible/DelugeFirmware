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

#include "deluge-project/mapping_codec.h"

#include "serialization/tree.h"

#include <cstdio>
#include <string>
#include <string_view>

namespace deluge::project {

using serialization::int_of;
using serialization::text_of;
using serialization::Value;

namespace {

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

Value encode_sections(const Arrangement& arrangement, const MappingTable& mappings) {
	auto list = Value::makeArray();
	for (int32_t s = 0; s < kMaxNumSections; s++) {
		const Section& section = arrangement.sections[s];
		auto map = Value::makeMap();
		map.set("id", Value::makeNumber(s));
		map.set("numRepeats", Value::makeNumber(section.num_repetitions));
		encode_section_command(mappings, static_cast<uint8_t>(s), map);
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

std::expected<void, LoadError> decode(const Value& song, Arrangement& arrangement, MappingTable& mappings,
                                      LegacyMacroTargets* legacy) {
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
			if (id >= kMaxNumSections) {
				continue;
			}
			decode_section_command(*node, static_cast<uint8_t>(id), mappings);
			arrangement.sections[id].num_repetitions = repeats;
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
