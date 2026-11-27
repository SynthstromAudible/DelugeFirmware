#include "clean/clean_check.h"

#include "controls_test_plugins.h"
#include "project/clean_codec.h"
#include "serialization/parse_json.h"
#include "serialization/parse_xml.h"
#include "serialization/write_json.h"

#include <algorithm>
#include <cstdint>

namespace deluge::project {
namespace {

using serialization::Value;
namespace sch = serialization::schema;

constexpr std::string_view kFirmware = "c2.0.0-dev";

const CodecPlugins& plugins() {
	static const CodecPlugins held = test::test_plugins();
	return held;
}

std::string joined(const std::string& path, std::string_view step) {
	return path.empty() ? std::string{step} : path + "/" + std::string{step};
}

/// Every path at which @p a and @p b differ: a scalar's text or kind, a map's keys, a list's length.
void differences(const Value& a, const Value& b, const std::string& path, std::vector<std::string>& found) {
	if (a.type() != b.type()) {
		found.push_back(path);
		return;
	}
	if (a.isScalar()) {
		if (!(a == b)) {
			found.push_back(path);
		}
		return;
	}
	if (a.isArray()) {
		if (a.items().size() != b.items().size()) {
			found.push_back(path);
			return;
		}
		for (std::size_t i = 0; i < a.items().size(); ++i) {
			differences(a.items()[i], b.items()[i], joined(path, std::to_string(i)), found);
		}
		return;
	}
	const auto& ea = a.entries();
	const auto& eb = b.entries();
	if (ea.size() != eb.size()
	    || !std::ranges::equal(ea, eb, {}, &std::pair<std::string, Value>::first,
	                           &std::pair<std::string, Value>::first)) {
		found.push_back(path);
		return;
	}
	for (std::size_t i = 0; i < ea.size(); ++i) {
		differences(ea[i].second, eb[i].second, joined(path, ea[i].first), found);
	}
}

/// The clamped paths of @p report, or the first note of any other kind.
std::expected<std::vector<std::string>, std::string> clamped_in(const sch::LoadReport& report) {
	std::vector<std::string> clamped;
	for (const sch::LoadNote& note : report.notes) {
		if (note.kind != sch::LoadNote::Kind::CLAMPED) {
			return std::unexpected("its clean load noted something other than a clamp at " + note.path);
		}
		clamped.push_back(note.path);
	}
	return clamped;
}

/// That @p before and @p after hold the same record but at @p clamped, and that @p after's clean save is stable.
template <typename R, typename Encode, typename Decode>
std::expected<void, std::string> survives(const R& before, const R& after, const std::vector<std::string>& clamped,
                                          Encode encode, Decode decode) {
	const Value wrote = encode(before);
	const Value rewrote = encode(after);
	std::vector<std::string> differ;
	differences(wrote, rewrote, {}, differ);
	for (const std::string& path : differ) {
		if (!std::ranges::contains(clamped, path)) {
			return std::unexpected("changed at " + path + ", which the load did not clamp");
		}
	}
	if (clamped.empty() && !(after == before)) {
		return std::unexpected("its model changed through the clean format");
	}
	const std::string text = serialization::write_json(rewrote);
	auto again = decode(*serialization::parseJson(text));
	if (!again || !again->report.empty() || serialization::write_json(encode(again->value)) != text) {
		return std::unexpected("a second clean save does not read back the same");
	}
	return {};
}

} // namespace

std::expected<CleanSummary, std::string> check_clean(std::string_view text, bool is_json, const CleanFaults& faults) {
	auto parsed = is_json ? serialization::parseJson(text) : serialization::parseXml(text);
	if (!parsed) {
		return std::unexpected("does not parse");
	}
	auto legacy = decode_song(*parsed, plugins());
	if (!legacy) {
		return std::unexpected("has no song");
	}
	Value clean = encode_clean(*legacy, kFirmware, plugins());
	if (faults.on_encoded) {
		faults.on_encoded(clean);
	}
	auto back = decode_clean(*serialization::parseJson(serialization::write_json(clean)), plugins());
	if (!back) {
		return std::unexpected("its clean save does not load, at " + back.error().path);
	}
	if (faults.on_decoded) {
		faults.on_decoded(back->value);
	}
	auto clamped = clamped_in(back->report);
	if (!clamped) {
		return std::unexpected(clamped.error());
	}
	const auto encode_song = [](const SongDocument& song) { return encode_clean(song, kFirmware, plugins()); };
	const auto decode_song_text = [](const Value& root) { return decode_clean(root, plugins()); };
	if (auto kept = survives(*legacy, back->value, *clamped, encode_song, decode_song_text); !kept) {
		return std::unexpected(kept.error());
	}
	CleanSummary summary{.clamped = std::move(*clamped)};

	const int32_t magnitude = legacy->transport.input_tick_magnitude();
	const auto encode_preset = [&](const PresetDocument& preset) {
		return encode_preset_clean(preset, magnitude, kFirmware, plugins());
	};
	const auto decode_preset = [](const Value& root) { return decode_preset_clean(root, plugins()); };
	for (const SongDocument::OutputEntry& entry : legacy->output_order) {
		const PresetDocument preset = extract_preset(*legacy, entry.id);
		auto read = decode_preset(*serialization::parseJson(serialization::write_json(encode_preset(preset))));
		if (!read) {
			return std::unexpected("output " + std::to_string(entry.id) + " as a preset does not load, at "
			                       + read.error().path);
		}
		auto preset_clamped = clamped_in(read->report);
		if (!preset_clamped) {
			return std::unexpected("output " + std::to_string(entry.id) + " as a preset: " + preset_clamped.error());
		}
		if (auto kept = survives(preset, read->value, *preset_clamped, encode_preset, decode_preset); !kept) {
			return std::unexpected("output " + std::to_string(entry.id) + " as a preset " + kept.error());
		}
		++summary.presets;
	}
	return summary;
}

} // namespace deluge::project
