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

#include "equality/equality.h"

#include "serialization/parse_json.h"
#include "serialization/parse_xml.h"
#include "serialization/write_json.h"

#include <format>

namespace deluge::project {

namespace {

/// Encoding the round trip writes the sync values relative to the tick magnitude, as every community firmware does.
constexpr std::string_view kWriter = "c1.0.0";

template <class Map>
std::string first_differing_id(const Map& a, const Map& b) {
	for (const auto& [id, record] : a) {
		auto it = b.find(id);
		if (it == b.end()) {
			return std::format("{} missing after the round trip", id);
		}
		if (!(it->second == record)) {
			return std::format("{} differs", id);
		}
	}
	return "an extra record after the round trip";
}

/// The first record that differs between @p a and @p b.
std::string difference(const SongDocument& a, const SongDocument& b) {
	if (!(a.transport == b.transport)) {
		return "transport";
	}
	if (!(a.groove == b.groove)) {
		return "groove";
	}
	if (!(a.scale == b.scale)) {
		return "scale";
	}
	if (!(a.reverb == b.reverb)) {
		return "reverb";
	}
	if (!(a.arrangement.sections == b.arrangement.sections)) {
		return "sections";
	}
	if (!(a.arrangement.session_macros == b.arrangement.session_macros)) {
		return "session macros";
	}
	if (!(a.clip_order == b.clip_order)) {
		return "clip order";
	}
	if (!(a.output_order == b.output_order)) {
		return "output order";
	}
	if (!(a.clips == b.clips)) {
		return "clip " + first_differing_id(a.clips, b.clips);
	}
	if (!(a.audio_clips == b.audio_clips)) {
		return "audio clip " + first_differing_id(a.audio_clips, b.audio_clips);
	}
	if (!(a.outputs == b.outputs)) {
		return "output " + first_differing_id(a.outputs, b.outputs);
	}
	return "drum " + first_differing_id(a.drums, b.drums);
}

} // namespace

std::expected<void, std::string> check_equality(std::string_view songText, bool isJson, const EqualityFaults& faults) {
	auto parsed = isJson ? serialization::parseJson(songText) : serialization::parseXml(songText);
	if (!parsed) {
		return std::unexpected("does not parse");
	}
	auto a = decode_song(*parsed);
	if (!a) {
		return std::unexpected("has no song");
	}
	serialization::Value encoded = encode_song(*a, kWriter);
	if (faults.on_encoded) {
		faults.on_encoded(encoded);
	}
	const std::string written = serialization::write_json(encoded);
	auto reparsed = serialization::parseJson(written);
	if (!reparsed) {
		return std::unexpected("its JSON does not parse");
	}
	auto b = decode_song(*reparsed);
	if (!b) {
		return std::unexpected("its JSON has no song");
	}
	if (faults.on_decoded) {
		faults.on_decoded(*b);
	}
	if (!(*b == *a)) {
		return std::unexpected("the " + difference(*a, *b) + " changed in the JSON round trip");
	}
	if (serialization::write_json(encode_song(*b, kWriter)) != serialization::write_json(encode_song(*a, kWriter))) {
		return std::unexpected("a second write differs");
	}
	return {};
}

} // namespace deluge::project
