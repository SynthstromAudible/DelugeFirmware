#include "music-theory/note_length.h"

#include "plugin-api/types/display_mapping.h"
#include "plugin-api/types/sync.h"

#include <algorithm>
#include <charconv>

namespace deluge::music {

namespace {

void append(NoteLengthName& name, std::string_view text) {
	const size_t room = NoteLengthName::kCapacity - name.size;
	const size_t count = std::min(room, text.size());
	std::copy_n(text.data(), count, name.text.data() + name.size);
	name.size += count;
}

void append_number(NoteLengthName& name, uint32_t value) {
	std::array<char, 10> digits{};
	const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), value);
	append(name, {digits.data(), static_cast<size_t>(result.ptr - digits.data())});
}

} // namespace

int32_t note_magnitude(uint32_t note_length, int32_t tick_magnitude) {
	int32_t magnitude = -5 - tick_magnitude;
	for (uint32_t level = 3; level < note_length; level <<= 1) {
		magnitude++;
	}
	return magnitude;
}

NoteLengthName note_length_name(int32_t magnitude, std::string_view suffix, bool per_column) {
	NoteLengthName name;
	magnitude = std::clamp(magnitude, -31, 31);
	if (magnitude < 0) {
		const uint32_t division = uint32_t{1} << -magnitude;
		append_number(name, division);
		// Divisions are powers of two, so only 2 takes "nd" (no "rd" is needed).
		append(name, (division % 10) == 2 ? "nd" : "th");
		append(name, suffix);
		return name;
	}
	const uint32_t bars = uint32_t{1} << magnitude;
	append_number(name, bars);
	if (per_column) {
		append(name, bars == 1 ? " bar (per column)" : " bars (per column)");
	}
	else {
		append(name, "-bar");
	}
	return name;
}

NoteLengthName sync_value_name(int32_t combined_value, int32_t tick_magnitude, SyncNameForm form) {
	const auto [level_index, type_choice] = plugin::combined_sync_stored(combined_value);
	const auto level = plugin::sync_level_from_index(level_index);
	const uint32_t shift = static_cast<uint32_t>(plugin::kMaxSyncLevel - plugin::index_of(level));
	const int32_t magnitude = note_magnitude(uint32_t{3} << shift, tick_magnitude);

	if (form == SyncNameForm::HorizontalLabel) {
		const NoteLengthName full = note_length_name(magnitude, "", false);
		NoteLengthName label;
		for (const char c : full.view()) {
			if (c != '-') {
				append(label, std::string_view{&c, 1});
			}
		}
		return label;
	}

	std::string_view suffix;
	if (type_choice == 1) {
		suffix = "-tplts";
	}
	else if (type_choice == 2) {
		suffix = "-dtted";
	}
	else if (combined_value != 0) {
		suffix = "-notes";
	}
	NoteLengthName name = note_length_name(magnitude, suffix, false);
	// A bar-length name has no suffix of its own, so a triplet or dotted bar gets it here.
	if (type_choice != 0 && plugin::index_of(level) <= plugin::index_of(plugin::SyncLevel::EIGHTH) - tick_magnitude) {
		append(name, suffix);
	}
	return name;
}

} // namespace deluge::music
