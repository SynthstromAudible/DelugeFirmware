#include "music-theory/note_length.h"

#include "plugin-api/types/display_mapping.h"
#include "plugin-api/types/sync.h"

#include <algorithm>

namespace deluge::music {

int32_t note_magnitude(uint32_t note_length, int32_t tick_magnitude) {
	int32_t magnitude = -5 - tick_magnitude;
	// Wide enough that the doubling passes any 32-bit length, so the loop ends.
	for (uint64_t level = 3; level < note_length; level <<= 1) {
		magnitude++;
	}
	return magnitude;
}

NoteLengthName note_length_name(int32_t magnitude, std::string_view suffix, bool per_column) {
	NoteLengthName name;
	magnitude = std::clamp(magnitude, -31, 31);
	if (magnitude < 0) {
		const uint32_t division = uint32_t{1} << -magnitude;
		name.append_number(division);
		// Divisions are powers of two, so only 2 takes "nd" (no "rd" is needed).
		name.append((division % 10) == 2 ? "nd" : "th");
		name.append(suffix);
		return name;
	}
	const uint32_t bars = uint32_t{1} << magnitude;
	name.append_number(bars);
	if (per_column) {
		name.append(bars == 1 ? " bar (per column)" : " bars (per column)");
	}
	else {
		name.append("-bar");
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
				label.append(std::string_view{&c, 1});
			}
		}
		return label;
	}

	const plugin::SyncType type = plugin::kSyncTypeChoices[static_cast<size_t>(type_choice)];
	std::string_view suffix;
	if (type == plugin::SyncType::TRIPLET) {
		suffix = "-tplts";
	}
	else if (type == plugin::SyncType::DOTTED) {
		suffix = "-dtted";
	}
	else if (combined_value != 0) {
		suffix = "-notes";
	}
	NoteLengthName name = note_length_name(magnitude, suffix, false);
	// A bar-length name has no suffix of its own, so a triplet or dotted bar gets it here.
	if (type != plugin::SyncType::EVEN && magnitude >= 0) {
		name.append(suffix);
	}
	return name;
}

} // namespace deluge::music
