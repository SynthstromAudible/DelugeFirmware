#include "deluge-project/arp_codec.h"

#include "engine-api/types/sync.h"
#include "serialization/tree.h"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>

namespace deluge::project {

using serialization::Value;

namespace {

/// The host values' attributes, in the order the firmware writes them.
constexpr std::array<std::string_view, 16> kHostOrder{"gate",
                                                      "rate",
                                                      "noteProbability",
                                                      "bassProbability",
                                                      "swapProbability",
                                                      "glideProbability",
                                                      "reverseProbability",
                                                      "chordProbability",
                                                      "ratchetProbability",
                                                      "ratchetAmount",
                                                      "sequenceLength",
                                                      "chordPolyphony",
                                                      "rhythm",
                                                      "spreadVelocity",
                                                      "spreadGate",
                                                      "spreadOctave"};

std::string path_of(std::string_view attribute) {
	return std::string{"arpeggiator/"} + std::string{attribute};
}

Value word(std::span<const std::string_view> words, uint8_t index) {
	return Value::makeScalar(std::string{index < words.size() ? words[index] : words.front()});
}

/// The index of @p element's @p attribute among @p words, or nullopt when absent. An unknown word is the first, as the
/// firmware's readers take it.
std::optional<uint8_t> word_index(const Value& element, std::string_view attribute,
                                  std::span<const std::string_view> words) {
	const std::string* text = serialization::text_of(element.find(attribute));
	if (text == nullptr) {
		return std::nullopt;
	}
	const auto at = std::ranges::find(words, *text);
	return static_cast<uint8_t>(at == words.end() ? 0 : at - words.begin());
}

} // namespace

Value encode(const ArpRecord& record, std::optional<int32_t> tick_magnitude) {
	Value map = encode_settings(record, tick_magnitude);
	for (const auto& [name, value] : encode_host(record).entries()) {
		map.set(name, value);
	}
	return map;
}

Value encode_settings(const ArpRecord& record, std::optional<int32_t> tick_magnitude) {
	Value map = Value::makeMap();
	const auto number = [](int64_t n) { return Value::makeNumber(n); };
	const int32_t sync =
	    tick_magnitude ? engine::sync_level_to_file(record.sync_level, *tick_magnitude) : int32_t{record.sync_level};
	map.set("mode", word(engine::kArpModeWords, record.mode));
	map.set("syncLevel", number(sync));
	map.set("numOctaves", number(record.num_octaves));
	map.set("syncType", number(record.sync_type));
	map.set("arpMode", word(engine::kArpModeWords, record.mode));
	map.set("chordType", number(record.chord_type));
	map.set("noteMode", word(engine::kArpNoteModeWords, record.note_mode));
	map.set("octaveMode", word(engine::kArpOctaveModeWords, record.octave_mode));
	map.set("mpeVelocity", word(engine::kArpMpeVelocityWords, record.mpe_velocity));
	map.set("stepRepeat", number(record.step_repeat));
	map.set("randomizerLock", number(record.randomizer_lock ? 1 : 0));
	map.set("kitArp", number(record.kit_arp ? 1 : 0));
	return map;
}

Value encode_host(const ArpRecord& record) {
	Value map = Value::makeMap();
	for (std::string_view attribute : kHostOrder) {
		if (const std::optional<int32_t> value = host_value(record, path_of(attribute))) {
			map.set(std::string{attribute}, Value::makeNumber(*value));
		}
	}
	return map;
}

void decode(const Value& arpElement, ArpRecord& record, std::optional<int32_t> tick_magnitude) {
	if (!arpElement.isMap()) {
		return;
	}
	const auto integer = [&](std::string_view attribute) { return serialization::int_of(arpElement.find(attribute)); };
	if (const auto mode = word_index(arpElement, "arpMode", engine::kArpModeWords)) {
		record.mode = *mode;
	}
	else if (const std::string* old = serialization::text_of(arpElement.find("mode"))) {
		// Before arpMode, one word named the mode and its direction.
		constexpr std::array<std::string_view, 5> kOldModes{"off", "up", "down", "both", "random"};
		const auto at = std::ranges::find(kOldModes, *old);
		const uint8_t preset = static_cast<uint8_t>(at == kOldModes.end() ? 0 : at - kOldModes.begin());
		const engine::ArpPresetFields fields = *engine::arp_preset_fields(preset);
		record.mode = fields.mode;
		record.octave_mode = fields.octave_mode.value_or(record.octave_mode);
		record.note_mode = fields.note_mode.value_or(record.note_mode);
	}
	if (const auto octave = word_index(arpElement, "octaveMode", engine::kArpOctaveModeWords)) {
		record.octave_mode = *octave;
	}
	if (const auto note = word_index(arpElement, "noteMode", engine::kArpNoteModeWords)) {
		record.note_mode = *note;
	}
	if (const auto mpe = word_index(arpElement, "mpeVelocity", engine::kArpMpeVelocityWords)) {
		record.mpe_velocity = *mpe;
	}
	if (const auto v = integer("numOctaves")) {
		record.num_octaves = static_cast<uint8_t>(*v);
	}
	if (const auto v = integer("stepRepeat")) {
		record.step_repeat = static_cast<uint8_t>(*v);
	}
	if (const auto v = integer("chordType"); v && *v >= 0 && *v < engine::kArpChordTypes) {
		record.chord_type = static_cast<uint8_t>(*v);
	}
	if (const auto v = integer("syncLevel")) {
		record.sync_level =
		    static_cast<uint8_t>(tick_magnitude ? engine::sync_level_from_file(*v, *tick_magnitude) : *v);
	}
	if (const auto v = integer("syncType")) {
		record.sync_type = static_cast<uint8_t>(*v);
	}
	if (const auto v = integer("randomizerLock")) {
		record.randomizer_lock = *v != 0;
	}
	if (const auto v = integer("kitArp")) {
		record.kit_arp = *v != 0;
	}
	for (std::string_view attribute : kHostOrder) {
		if (const auto v = integer(attribute)) {
			set_host_value(record, path_of(attribute), static_cast<int32_t>(*v));
		}
	}
}

} // namespace deluge::project
